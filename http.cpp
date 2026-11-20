// ============================================================================
//  http.cpp - Socket handling, request parsing, response writing.
// ============================================================================
#include "http.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using SocketType = SOCKET;
#define CLOSE_SOCKET closesocket
#define BAD_SOCKET INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketType = int;
#define CLOSE_SOCKET close
#define BAD_SOCKET (-1)
#endif

namespace {

const char* statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

void parseQueryString(const std::string& raw, std::map<std::string, std::string>& out) {
    std::size_t start = 0;
    while (start < raw.size()) {
        std::size_t amp = raw.find('&', start);
        std::string pair = raw.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!pair.empty()) {
            std::size_t eq = pair.find('=');
            std::string key = eq == std::string::npos ? pair : pair.substr(0, eq);
            std::string value = eq == std::string::npos ? "" : pair.substr(eq + 1);
            out[HttpServer::urlDecode(key)] = HttpServer::urlDecode(value);
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
}

// Extremely small "flat object" JSON reader: {"a": "x", "b": 3}
// Enough for the POST bodies used by the frontend, and easy to read.
void parseFlatJson(const std::string& body, std::map<std::string, std::string>& out) {
    std::size_t i = 0;
    auto skip = [&](const char* chars) {
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
        if (i < body.size() && std::strchr(chars, body[i])) ++i;
        while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i;
    };

    skip("{");
    while (i < body.size() && body[i] != '}') {
        std::string key;
        if (body[i] == '"') {
            ++i;
            while (i < body.size() && body[i] != '"') {
                if (body[i] == '\\' && i + 1 < body.size()) ++i;
                key += body[i++];
            }
            ++i;
        } else {
            while (i < body.size() && body[i] != ':' && body[i] != ',') key += body[i++];
        }
        while (i < body.size() && (body[i] == ':' || std::isspace(static_cast<unsigned char>(body[i])))) ++i;

        std::string value;
        if (i < body.size() && body[i] == '"') {
            ++i;
            while (i < body.size() && body[i] != '"') {
                if (body[i] == '\\' && i + 1 < body.size()) ++i;
                value += body[i++];
            }
            ++i;
        } else {
            while (i < body.size() && body[i] != ',' && body[i] != '}') value += body[i++];
        }
        out[key] = value;
        skip(",");
    }
}

}  // namespace

// ===========================================================================
//  HttpRequest helpers
// ===========================================================================
bool HttpRequest::has(const std::string& key) const {
    return query.count(key) > 0 || form.count(key) > 0;
}

std::string HttpRequest::param(const std::string& key, const std::string& fallback) const {
    auto q = query.find(key);
    if (q != query.end()) return q->second;
    auto f = form.find(key);
    if (f != form.end()) return f->second;
    return fallback;
}

int HttpRequest::paramInt(const std::string& key, int fallback) const {
    std::string raw = param(key);
    if (raw.empty()) return fallback;
    try {
        return std::stoi(raw);
    } catch (...) {
        return fallback;
    }
}

double HttpRequest::paramDouble(const std::string& key, double fallback) const {
    std::string raw = param(key);
    if (raw.empty()) return fallback;
    try {
        return std::stod(raw);
    } catch (...) {
        return fallback;
    }
}

// ===========================================================================
//  HttpResponse helpers
// ===========================================================================
HttpResponse HttpResponse::json(const std::string& body, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "application/json; charset=utf-8";
    response.body = body;
    return response;
}

HttpResponse HttpResponse::html(const std::string& body, int status) {
    HttpResponse response;
    response.status = status;
    response.contentType = "text/html; charset=utf-8";
    response.body = body;
    return response;
}

// ===========================================================================
//  HttpServer
// ===========================================================================
std::string HttpServer::urlDecode(const std::string& encoded) {
    std::string out;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        char c = encoded[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < encoded.size()) {
            int value = 0;
            std::istringstream(encoded.substr(i + 1, 2)) >> std::hex >> value;
            out += static_cast<char>(value);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

bool HttpServer::listenOn(int port, std::string& error) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "WSAStartup failed";
        return false;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);   // a closed browser tab must not kill us
#endif

    SocketType fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == BAD_SOCKET) {
        error = "could not create socket";
        return false;
    }

    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);   // listen on 0.0.0.0
    address.sin_port = htons(static_cast<unsigned short>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "port " + std::to_string(port) + " is already in use";
        CLOSE_SOCKET(fd);
        return false;
    }
    if (listen(fd, 32) != 0) {
        error = "listen failed";
        CLOSE_SOCKET(fd);
        return false;
    }

    listenFd_ = static_cast<long long>(fd);
    return true;
}

void HttpServer::run() {
    while (listenFd_ >= 0) {
        sockaddr_in clientAddress{};
#ifdef _WIN32
        int length = sizeof(clientAddress);
#else
        socklen_t length = sizeof(clientAddress);
#endif
        SocketType client = accept(static_cast<SocketType>(listenFd_),
                                   reinterpret_cast<sockaddr*>(&clientAddress), &length);
        if (client == BAD_SOCKET) continue;

        // A thrown exception must never take the whole server down.
        HttpResponse response;
        try {
            response = handleConnection(static_cast<long long>(client));
        } catch (const std::exception& failure) {
            std::cerr << "request failed: " << failure.what() << "\n";
            response = HttpResponse::json("{\"error\":\"internal server error\"}", 500);
        }

        std::string raw;
        raw += "HTTP/1.1 " + std::to_string(response.status) + " " + statusText(response.status) + "\r\n";
        raw += "Content-Type: " + response.contentType + "\r\n";
        raw += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
        raw += "Cache-Control: no-store\r\n";
        raw += "Access-Control-Allow-Origin: *\r\n";
        raw += "Connection: close\r\n\r\n";
        raw += response.body;

        std::size_t sent = 0;
        while (sent < raw.size()) {
            int n = send(client, raw.c_str() + sent,
                         static_cast<int>(raw.size() - sent), 0);
            if (n <= 0) break;
            sent += static_cast<std::size_t>(n);
        }

        CLOSE_SOCKET(client);
    }
}

HttpResponse HttpServer::handleConnection(long long clientFd) {
    SocketType client = static_cast<SocketType>(clientFd);

    std::string buffer;
    char chunk[8192];
    std::size_t headerEnd = std::string::npos;

    // ---- read headers -----------------------------------------------------
    while (headerEnd == std::string::npos && buffer.size() < 1024 * 1024) {
        int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) return HttpResponse::json("{\"error\":\"bad request\"}", 400);
        buffer.append(chunk, static_cast<std::size_t>(n));
        headerEnd = buffer.find("\r\n\r\n");
    }
    if (headerEnd == std::string::npos) {
        return HttpResponse::json("{\"error\":\"incomplete headers\"}", 400);
    }

    std::string headerPart = buffer.substr(0, headerEnd);
    std::string bodyPart = buffer.substr(headerEnd + 4);

    HttpRequest request;
    std::istringstream head(headerPart);
    std::string requestLine;
    std::getline(head, requestLine);
    if (!requestLine.empty() && requestLine.back() == '\r') requestLine.pop_back();
    std::string target;
    {
        std::string httpVersion;
        std::istringstream line(requestLine);
        line >> request.method >> target >> httpVersion;
    }

    // ---- headers ----------------------------------------------------------
    std::map<std::string, std::string> headers;
    std::string line;
    while (std::getline(head, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        headers[key] = value;
    }

    // ---- body -------------------------------------------------------------
    std::size_t contentLength = 0;
    auto it = headers.find("content-length");
    if (it != headers.end()) contentLength = static_cast<std::size_t>(std::strtoul(it->second.c_str(), nullptr, 10));

    constexpr std::size_t kMaxBodyBytes = 1024 * 1024;   // every real request is tiny
    if (contentLength > kMaxBodyBytes) {
        return HttpResponse::json("{\"error\":\"request body too large\"}", 400);
    }

    while (bodyPart.size() < contentLength) {
        int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        bodyPart.append(chunk, static_cast<std::size_t>(n));
    }
    request.body = bodyPart.substr(0, contentLength);

    // ---- path + query -----------------------------------------------------
    std::size_t question = target.find('?');
    std::string rawPath = question == std::string::npos ? target : target.substr(0, question);
    std::string rawQuery = question == std::string::npos ? "" : target.substr(question + 1);
    request.path = urlDecode(rawPath);
    parseQueryString(rawQuery, request.query);

    if (!request.body.empty()) {
        std::string trimmed = request.body;
        std::size_t begin = trimmed.find_first_not_of(" \r\n\t");
        if (begin != std::string::npos && trimmed[begin] == '{') parseFlatJson(request.body, request.form);
    }

    if (request.path.empty()) request.path = "/";
    return handler_(request);
}
