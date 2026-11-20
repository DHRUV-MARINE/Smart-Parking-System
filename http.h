// ============================================================================
//  http.h - A very small HTTP/1.1 server written from scratch on top of
//  POSIX sockets / Winsock.  No framework, so the whole stack stays readable.
//
//  It handles sequential requests (fine for a demo / lab project):
//    * static files  -> served by main.cpp
//    * /api/*        -> JSON REST endpoints
// ============================================================================
#pragma once

#include <functional>
#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;   // ?a=1&b=2
    std::map<std::string, std::string> form;    // flat JSON body {"a":1}
    std::string body;

    bool has(const std::string& key) const;
    // Looks in the query string first, then in the JSON body.
    std::string param(const std::string& key, const std::string& fallback = "") const;
    int paramInt(const std::string& key, int fallback) const;
    double paramDouble(const std::string& key, double fallback) const;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;

    static HttpResponse json(const std::string& body, int status = 200);
    static HttpResponse html(const std::string& body, int status = 200);
};

class HttpServer {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit HttpServer(Handler handler) : handler_(std::move(handler)) {}

    bool listenOn(int port, std::string& error);
    void run();      // blocks forever, serving requests one at a time

    static std::string urlDecode(const std::string& encoded);

private:
    HttpResponse handleConnection(long long client);
    Handler handler_;
    long long listenFd_ = -1;
};
