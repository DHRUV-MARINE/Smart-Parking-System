// ============================================================================
//  main.cpp - SmartParking entry point.
//
//  Builds the parking engine from SQLite, wires up the REST API and serves the
//  static frontend.  Run with --demo to print a console demonstration of every
//  DSA structure used in the project (handy for the viva).
// ============================================================================
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "booking.h"
#include "database.h"
#include "http.h"
#include "json.h"
#include "parking.h"
#include "sorting.h"

namespace fs = std::filesystem;

struct App {
    Database db;
    ParkingManager parking;
    BookingManager bookings;
    std::string webRoot;

    App() : bookings(parking, db) {}
};

// ===========================================================================
//  Small helpers
// ===========================================================================
static HttpResponse errorResponse(const std::string& message, int status) {
    Json json;
    json.beginObject();
    json.add("error", message);
    json.endObject();
    return HttpResponse::json(json.str(), status);
}

static HttpResponse okResponse(const Json& json) {
    return HttpResponse::json(json.str(), 200);
}

static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : path) {
        if (c == '/') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

static std::vector<std::string> splitComma(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) parts.push_back(current);
    return parts;
}

// Strict positive integer parser: rejects "1abc" instead of reading 1.
static int parseId(const std::string& text) {
    if (text.empty()) return 0;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return 0;
    }
    return std::atoi(text.c_str());
}

static std::string round1(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

static void writeSlot(Json& json, const Slot& slot) {
    json.beginObject();
    json.add("id", slot.id);
    json.add("label", slot.label);
    json.add("locationId", slot.locationId);
    json.add("occupied", slot.occupied);
    json.endObject();
}

static void writeBooking(Json& json, const Booking& booking) {
    json.beginObject();
    json.add("id", booking.id);
    json.add("locationId", booking.locationId);
    json.add("locationName", booking.locationName);
    json.add("slotId", booking.slotId);
    json.add("slotLabel", booking.slotLabel);
    json.add("userName", booking.userName);
    json.add("hours", booking.hours);
    json.add("amount", booking.amount);
    json.add("status", booking.status);
    json.add("createdAt", booking.createdAt);
    json.endObject();
}

static void writeLocation(Json& json, const ParkingLocation& loc,
                          double distanceKm, bool reachable, int waiting) {
    json.beginObject();
    json.add("id", loc.id);
    json.add("name", loc.name);
    json.add("address", loc.address);
    json.add("area", loc.area);
    json.add("lat", loc.lat);
    json.add("lng", loc.lng);
    json.add("totalSlots", loc.totalSlots);
    json.add("availableSlots", loc.availableSlots);
    json.add("occupiedSlots", loc.totalSlots - loc.availableSlots);
    json.add("status", availabilityStatus(loc.availableSlots, loc.totalSlots));
    json.add("pricePerHour", loc.pricePerHour);
    json.add("price3Hours", loc.price3Hours);
    json.add("pricePerDay", loc.pricePerDay);
    json.add("type", loc.type);
    json.add("rating", loc.rating);
    json.add("openHours", loc.openHours);
    json.add("distanceKm", distanceKm);
    json.add("reachable", reachable);
    json.add("waiting", waiting);
    json.endObject();
}

// Resolves ?place= (landmark or area name) or browser lat/lng to a graph vertex.
static int resolveOrigin(const ParkingManager& parking, const std::string& place,
                         bool hasCoords, double lat, double lng) {
    int node = parking.nodeIndexForPlace(place);
    if (node < 0 && hasCoords) node = parking.nearestNodeIndex(lat, lng);
    return node;
}

static int originFromRequest(const App& app, const HttpRequest& request) {
    return resolveOrigin(app.parking, request.param("place"),
                         request.has("lat") && request.has("lng"),
                         request.paramDouble("lat", 0.0), request.paramDouble("lng", 0.0));
}

static SearchQuery queryFromRequest(const HttpRequest& request) {
    SearchQuery query;
    query.text = request.param("q");
    query.place = request.param("place");
    query.lat = request.paramDouble("lat", 0.0);
    query.lng = request.paramDouble("lng", 0.0);
    query.hasCoords = request.has("lat") && request.has("lng");
    query.sortBy = request.param("sort", "distance");
    query.maxPrice = request.paramDouble("maxPrice", 0.0);
    query.maxDistanceKm = request.paramDouble("maxDistance", 0.0);
    query.minAvailable = request.paramInt("minAvailable", -1);
    query.type = request.param("type");
    query.includeFull = request.paramInt("includeFull", 1) != 0;
    query.limit = request.paramInt("limit", 0);
    return query;
}

// ===========================================================================
//  API handlers
// ===========================================================================
static HttpResponse apiHealth(App& app) {
    Json json;
    json.beginObject();
    json.add("ok", true);
    json.add("service", "SmartParking C++ backend");
    json.add("database", app.db.isOpen() ? "connected" : "offline");
    json.add("parkingLocations", static_cast<int>(app.parking.locations().size()));
    json.add("graphNodes", app.parking.graph().nodeCount());
    json.add("graphEdges", app.parking.graph().edgeCount());
    json.add("algorithms", "Dijkstra, hash table, merge sort, quick sort, binary search, "
                           "linked list, queue");
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiOrigins(App& app) {
    Json json;
    json.beginObject();
    json.key("origins").beginArray();
    for (const OriginOption& origin : app.parking.origins()) {
        json.beginObject();
        json.add("name", origin.name);
        json.add("lat", origin.lat);
        json.add("lng", origin.lng);
        json.endObject();
    }
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiLocations(App& app, const HttpRequest& request) {
    SearchQuery query = queryFromRequest(request);

    int originNode = resolveOrigin(app.parking, query.place, query.hasCoords,
                                   query.lat, query.lng);

    std::vector<SearchResult> results = app.parking.search(query);

    Json json;
    json.beginObject();
    json.add("originName", originNode >= 0 ? app.parking.nodeName(originNode) : std::string());
    json.add("sort", query.sortBy);
    json.add("count", static_cast<int>(results.size()));
    json.key("results").beginArray();
    for (const SearchResult& result : results) {
        writeLocation(json, result.loc, result.distanceKm, result.reachable,
                      app.parking.waitingCount(result.loc.id));
    }
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiLocationDetail(App& app, int id, const HttpRequest& request) {
    const ParkingLocation* loc = app.parking.findById(id);
    if (!loc) return errorResponse("parking location not found", 404);

    double distanceKm = 0.0;
    bool reachable = false;
    std::vector<std::string> route;

    int originNode = originFromRequest(app, request);
    if (originNode >= 0) {
        reachable = app.parking.routeTo(originNode, id, route, distanceKm);
    }

    Json json;
    json.beginObject();
    json.key("location");
    writeLocation(json, *loc, distanceKm, reachable, app.parking.waitingCount(id));
    json.key("slots").beginArray();
    for (const Slot& slot : app.parking.slotsOf(id)) writeSlot(json, slot);
    json.endArray();
    json.key("waitingList").beginArray();
    for (const WaitEntry& entry : app.bookings.waitersFor(id)) {
        json.beginObject();
        json.add("id", entry.id);
        json.add("userName", entry.userName);
        json.endObject();
    }
    json.endArray();
    json.key("route").beginArray();
    for (const std::string& step : route) json.value(step);
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiSlots(App& app, int id) {
    const ParkingLocation* loc = app.parking.findById(id);
    if (!loc) return errorResponse("parking location not found", 404);

    Json json;
    json.beginObject();
    json.add("locationId", id);
    json.add("name", loc->name);
    json.add("totalSlots", loc->totalSlots);
    json.add("availableSlots", loc->availableSlots);
    json.add("status", availabilityStatus(loc->availableSlots, loc->totalSlots));
    json.key("slots").beginArray();
    for (const Slot& slot : app.parking.slotsOf(id)) writeSlot(json, slot);
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiRoute(App& app, int id, const HttpRequest& request) {
    if (!app.parking.findById(id)) return errorResponse("parking location not found", 404);
    int originNode = originFromRequest(app, request);
    if (originNode < 0) return errorResponse("unknown origin, pass place= or lat=&lng=", 400);

    std::vector<std::string> steps;
    double km = 0.0;
    bool found = app.parking.routeTo(originNode, id, steps, km);

    Json json;
    json.beginObject();
    json.add("from", app.parking.nodeName(originNode));
    json.add("to", app.parking.findById(id)->name);
    json.add("distanceKm", km);
    json.add("reachable", found);
    json.add("algorithm", "Dijkstra (binary min-heap priority queue)");
    json.key("path").beginArray();
    for (const std::string& step : steps) json.value(step);
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiCreateBooking(App& app, const HttpRequest& request) {
    int locationId = request.paramInt("locationId", 0);
    int slotId = request.paramInt("slotId", 0);
    int hours = request.paramInt("hours", 2);
    std::string user = request.param("user");

    Booking booking;
    std::string error;
    if (!app.bookings.book(locationId, slotId, user, hours, booking, error)) {
        return errorResponse(error, 409);
    }

    const ParkingLocation* loc = app.parking.findById(locationId);

    Json json;
    json.beginObject();
    json.add("success", true);
    json.add("message", "Slot " + booking.slotLabel + " booked at " + booking.locationName);
    json.key("booking");
    writeBooking(json, booking);
    json.key("location");
    writeLocation(json, *loc, 0.0, false, app.parking.waitingCount(locationId));
    json.endObject();
    return HttpResponse::json(json.str(), 201);
}

static HttpResponse apiCancelBooking(App& app, int bookingId, const HttpRequest& request) {
    Booking cancelled;
    Booking promotedBooking;
    bool promoted = false;
    std::string error;

    if (!app.bookings.cancel(bookingId, request.param("user"), cancelled, promoted,
                             promotedBooking, error)) {
        return errorResponse(error, 409);
    }

    Json json;
    json.beginObject();
    json.add("success", true);
    json.add("message", "Booking #" + std::to_string(cancelled.id) + " cancelled");
    json.key("booking");
    writeBooking(json, cancelled);
    json.add("promoted", promoted);
    if (promoted) {
        json.key("promotedBooking");
        writeBooking(json, promotedBooking);
    }
    const ParkingLocation* loc = app.parking.findById(cancelled.locationId);
    if (loc) {
        json.key("location");
        writeLocation(json, *loc, 0.0, false, app.parking.waitingCount(loc->id));
    }
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiBookingDetail(App& app, int bookingId) {
    for (const Booking& booking : app.bookings.allActive()) {
        if (booking.id == bookingId) {
            Json json;
            json.beginObject();
            json.key("booking");
            writeBooking(json, booking);
            const ParkingLocation* loc = app.parking.findById(booking.locationId);
            if (loc) {
                json.key("location");
                writeLocation(json, *loc, 0.0, false, app.parking.waitingCount(loc->id));
            }
            json.endObject();
            return okResponse(json);
        }
    }
    return errorResponse("booking not found or already cancelled", 404);
}

static HttpResponse apiBookings(App& app, const HttpRequest& request) {
    std::string user = request.param("user");
    std::vector<Booking> list = user.empty() ? app.bookings.allActive()
                                             : app.bookings.bookingsFor(user);
    Json json;
    json.beginObject();
    json.add("count", static_cast<int>(list.size()));
    json.key("bookings").beginArray();
    for (const Booking& booking : list) writeBooking(json, booking);
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiJoinWaitlist(App& app, const HttpRequest& request) {
    int locationId = request.paramInt("locationId", 0);
    std::string user = request.param("user");
    WaitEntry entry;
    bool already = false;
    std::string error;

    if (!app.bookings.joinWaitlist(locationId, user, entry, already, error)) {
        return errorResponse(error, 409);
    }

    Json json;
    json.beginObject();
    json.add("success", true);
    json.add("alreadyWaiting", already);
    json.add("message", already ? "You are already on this waiting list"
                                : "Added to the waiting queue for this parking lot");
    json.add("position", app.parking.waitingCount(locationId));
    json.add("locationId", locationId);
    json.endObject();
    return HttpResponse::json(json.str(), 201);
}

static HttpResponse apiStats(App& app) {
    int totalSlots = app.parking.totalSlotsAll();
    int available = app.parking.availableSlotsAll();
    int waitingTotal = 0;

    Json json;
    json.beginObject();
    json.add("locations", static_cast<int>(app.parking.locations().size()));
    json.add("totalSlots", totalSlots);
    json.add("availableSlots", available);
    json.add("occupiedSlots", totalSlots - available);
    json.add("occupancyPercent", totalSlots > 0 ? (100.0 * (totalSlots - available) / totalSlots) : 0.0);
    json.add("activeBookings", app.bookings.activeCount());
    json.add("revenue", app.bookings.revenue());
    json.add("graphNodes", app.parking.graph().nodeCount());
    json.add("graphEdges", app.parking.graph().edgeCount());

    struct Row {
        int id;
        std::string name;
        std::string type;
        int total;
        int available;
        int waiting;
        double price;
    };
    std::vector<Row> rows;
    for (const ParkingLocation& loc : app.parking.locations()) {
        int waiting = app.parking.waitingCount(loc.id);
        waitingTotal += waiting;
        rows.push_back(Row{loc.id, loc.name, loc.type, loc.totalSlots, loc.availableSlots, waiting,
                           loc.pricePerHour});
    }

    std::vector<Row> busiest = rows;
    mergeSort(busiest, [](const Row& a, const Row& b) {
        double occupancyA = a.total > 0 ? static_cast<double>(a.total - a.available) / a.total : 0;
        double occupancyB = b.total > 0 ? static_cast<double>(b.total - b.available) / b.total : 0;
        return occupancyA > occupancyB;
    });

    json.key("lots").beginArray();
    for (const Row& row : rows) {
        json.beginObject();
        json.add("id", row.id);
        json.add("name", row.name);
        json.add("type", row.type);
        json.add("totalSlots", row.total);
        json.add("availableSlots", row.available);
        json.add("occupiedSlots", row.total - row.available);
        json.add("occupancyPercent", row.total > 0 ? (100.0 * (row.total - row.available) / row.total) : 0.0);
        json.add("status", availabilityStatus(row.available, row.total));
        json.add("waiting", row.waiting);
        json.add("pricePerHour", row.price);
        json.endObject();
    }
    json.endArray();

    json.key("busiest").beginArray();
    for (std::size_t i = 0; i < busiest.size() && i < 5; ++i) {
        json.beginObject();
        json.add("id", busiest[i].id);
        json.add("name", busiest[i].name);
        json.add("occupancyPercent",
                 busiest[i].total > 0
                     ? (100.0 * (busiest[i].total - busiest[i].available) / busiest[i].total)
                     : 0.0);
        json.endObject();
    }
    json.endArray();
    json.add("waitingTotal", waitingTotal);
    json.key("bookings").beginArray();
    for (const Booking& booking : app.bookings.allActive()) writeBooking(json, booking);
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiCompare(App& app, const HttpRequest& request) {
    int hours = request.paramInt("hours", 3);
    if (hours < 1) hours = 1;
    if (hours > 168) hours = 168;

    struct Fare {
        ParkingLocation loc;
        double amount;
    };
    std::vector<Fare> fares;
    for (const std::string& raw : splitComma(request.param("ids"))) {
        int id = parseId(raw);
        const ParkingLocation* loc = app.parking.findById(id);
        if (loc) fares.push_back(Fare{*loc, costForHours(*loc, hours)});
    }

    // Quick sort: cheapest stay first.
    quickSort(fares, [](const Fare& a, const Fare& b) { return a.amount < b.amount; });

    Json json;
    json.beginObject();
    json.add("hours", hours);
    json.key("results").beginArray();
    for (const Fare& fare : fares) {
        json.beginObject();
        json.add("id", fare.loc.id);
        json.add("name", fare.loc.name);
        json.add("pricePerHour", fare.loc.pricePerHour);
        json.add("price3Hours", fare.loc.price3Hours);
        json.add("pricePerDay", fare.loc.pricePerDay);
        json.add("amount", fare.amount);
        json.add("availableSlots", fare.loc.availableSlots);
        json.endObject();
    }
    json.endArray();
    json.endObject();
    return okResponse(json);
}

static HttpResponse apiGraph(App& app, const HttpRequest& request) {
    std::string place = request.param("place");
    int originNode = originFromRequest(app, request);

    Json json;
    json.beginObject();
    json.add("nodes", app.parking.graph().nodeCount());
    json.add("edges", app.parking.graph().edgeCount());
    json.add("algorithm", "Dijkstra with a hand-written binary min-heap");
    json.add("origin", originNode >= 0 ? app.parking.nodeName(originNode) : "");

    json.key("distances").beginArray();
    if (originNode >= 0) {
        SearchQuery query;
        query.place = place;
        query.sortBy = "distance";
        query.includeFull = true;
        for (const SearchResult& result : app.parking.search(query)) {
            json.beginObject();
            json.add("locationId", result.loc.id);
            json.add("name", result.loc.name);
            json.add("distanceKm", result.distanceKm);
            std::vector<std::string> steps;
            double km = 0.0;
            app.parking.routeTo(originNode, result.loc.id, steps, km);
            json.key("path").beginArray();
            for (const std::string& step : steps) json.value(step);
            json.endArray();
            json.endObject();
        }
    }
    json.endArray();
    json.endObject();
    return okResponse(json);
}

// ===========================================================================
//  Static files
// ===========================================================================
static std::string mimeType(const std::string& extension) {
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "application/javascript; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".webp") return "image/webp";
    if (extension == ".ico") return "image/x-icon";
    if (extension == ".woff2") return "font/woff2";
    return "application/octet-stream";
}

static HttpResponse serveStatic(const App& app, const HttpRequest& request) {
    std::string relative = request.path;
    if (relative == "/") relative = "/index.html";
    // Reject traversal on both separators so a Windows build is safe too.
    if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos) {
        return errorResponse("invalid path", 400);
    }
    if (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());

    fs::path file = fs::path(app.webRoot) / relative;
    std::error_code ec;
    if (!fs::exists(file, ec) || fs::is_directory(file, ec)) {
        return HttpResponse::html(
            "<!doctype html><html><body style=\"font-family:system-ui;padding:40px\">"
            "<h1>404 - Page not found</h1>"
            "<a href=\"/\">Back to SmartParking</a></body></html>",
            404);
    }

    std::ifstream input(file, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();

    HttpResponse response;
    response.status = 200;
    response.contentType = mimeType(file.extension().string());
    response.body = buffer.str();
    return response;
}

// ===========================================================================
//  Router
// ===========================================================================
static HttpResponse dispatch(App& app, const HttpRequest& request) {
    std::vector<std::string> parts = splitPath(request.path);

    if (!parts.empty() && parts[0] == "api") {
        if (parts.size() == 1) return errorResponse("unknown API route", 404);

        const std::string& resource = parts[1];

        if (resource == "health") return apiHealth(app);
        if (resource == "origins") return apiOrigins(app);
        if (resource == "stats") return apiStats(app);
        if (resource == "graph") return apiGraph(app, request);
        if (resource == "compare") return apiCompare(app, request);

        if (resource == "locations") {
            if (parts.size() == 2) return apiLocations(app, request);
            int id = parseId(parts[2]);
            if (id <= 0) return errorResponse("invalid parking id", 400);
            if (parts.size() == 3) return apiLocationDetail(app, id, request);
            if (parts.size() == 4 && parts[3] == "slots") return apiSlots(app, id);
            if (parts.size() == 4 && parts[3] == "route") return apiRoute(app, id, request);
            return errorResponse("unknown locations route", 404);
        }

        if (resource == "bookings") {
            if (parts.size() == 2) {
                if (request.method == "POST") return apiCreateBooking(app, request);
                return apiBookings(app, request);
            }
            int id = parseId(parts[2]);
            if (id <= 0) return errorResponse("invalid booking id", 400);
            if (parts.size() == 3) {
                if (request.method == "POST") return errorResponse("use POST /api/bookings", 405);
                return apiBookingDetail(app, id);
            }
            if (parts.size() == 4 && parts[3] == "cancel") {
                if (request.method != "POST") return errorResponse("use POST to cancel", 405);
                return apiCancelBooking(app, id, request);
            }
            return errorResponse("unknown bookings route", 404);
        }

        if (resource == "waitlist") {
            if (request.method != "POST") return errorResponse("use POST to join a waiting list", 405);
            return apiJoinWaitlist(app, request);
        }

        return errorResponse("unknown API route", 404);
    }

    return serveStatic(app, request);
}

// ===========================================================================
//  Data loading / seeding
// ===========================================================================
static std::string findPath(const std::vector<std::string>& candidates,
                            const std::string& argv0) {
    std::vector<fs::path> roots;
    roots.push_back(fs::current_path());
    if (!argv0.empty()) {
        std::error_code ec;
        fs::path exe = fs::absolute(argv0, ec);
        if (!ec) roots.push_back(exe.parent_path());
    }
    for (const fs::path& root : roots) {
        for (const std::string& candidate : candidates) {
            std::error_code ec;
            fs::path full = root / candidate;
            if (fs::exists(full, ec)) return full.lexically_normal().string();
        }
    }
    return candidates.empty() ? std::string() : candidates.front();
}

static bool seedSlots(Database& db, std::string& error) {
    int slotCount = 0;
    if (!db.countRows("slots", slotCount)) {
        error = db.lastError();
        return false;
    }
    if (slotCount > 0) return true;

    std::vector<ParkingLocation> locations;
    if (!db.loadLocations(locations)) {
        error = db.lastError();
        return false;
    }

    for (const ParkingLocation& loc : locations) {
        int occupiedTarget = loc.totalSlots * loc.occupancySeed / 100;
        for (int i = 0; i < loc.totalSlots; ++i) {
            char label[16];
            std::snprintf(label, sizeof(label), "%c-%02d", static_cast<char>('A' + i / 20), (i % 20) + 1);
            int newId = 0;
            if (!db.insertSlot(loc.id, label, i < occupiedTarget, newId)) {
                error = db.lastError();
                return false;
            }
        }
    }
    return true;
}

static bool prepareData(App& app, const std::string& schemaPath, std::string& error) {
    if (!app.db.runFile(schemaPath)) {
        error = "could not load " + schemaPath + ": " + app.db.lastError();
        return false;
    }
    if (!seedSlots(app.db, error)) return false;
    if (!app.parking.load(app.db, error)) return false;
    if (!app.bookings.load(error)) return false;
    return true;
}

// ===========================================================================
//  Console demonstration (--demo)
// ===========================================================================
static void printSeparator() {
    std::cout << std::string(74, '-') << "\n";
}

static void runDemo(App& app) {
    std::cout << "\n=== SmartParking - DSA demonstration ===\n\n";

    std::cout << "[1] GRAPH + DIJKSTRA\n";
    std::cout << "    graph vertices: " << app.parking.graph().nodeCount()
              << ", road edges: " << app.parking.graph().edgeCount() << "\n";
    SearchQuery nearby;
    nearby.place = "Mall Road";
    nearby.sortBy = "distance";
    std::vector<SearchResult> byDistance = app.parking.search(nearby);
    std::cout << "    shortest road distance from Mall Road:\n";
    for (std::size_t i = 0; i < byDistance.size() && i < 5; ++i) {
        std::cout << "      " << std::left << std::setw(36) << byDistance[i].loc.name
                  << std::right << std::setw(8) << round1(byDistance[i].distanceKm) << " km\n";
    }
    std::vector<std::string> steps;
    double km = 0.0;
    int origin = app.parking.nodeIndexForPlace("Mall Road");
    if (origin >= 0 && !byDistance.empty() &&
        app.parking.routeTo(origin, byDistance.front().loc.id, steps, km)) {
        std::cout << "    route: ";
        for (std::size_t i = 0; i < steps.size(); ++i) {
            if (i) std::cout << " -> ";
            std::cout << steps[i];
        }
        std::cout << "  (" << round1(km) << " km)\n";
    }

    std::cout << "\n[2] HASH TABLE lookup by id\n";
    if (const ParkingLocation* loc = app.parking.findById(7)) {
        std::cout << "    id 7 -> " << loc->name << " (" << loc->availableSlots
                  << "/" << loc->totalSlots << " free)\n";
    }
    std::cout << "    id 999 -> " << (app.parking.findById(999) ? "found" : "not found") << "\n";

    std::cout << "\n[3] SORTING - cheapest first (quick sort)\n";
    SearchQuery cheap = nearby;
    cheap.sortBy = "price";
    for (const SearchResult& result : app.parking.search(cheap)) {
        std::cout << "      Rs " << std::setw(5) << result.loc.pricePerHour << "/hr   "
                  << result.loc.name << "\n";
    }

    std::cout << "\n[4] SEARCH - substring 'sanjauli'\n";
    SearchQuery textQuery;
    textQuery.text = "sanjauli";
    for (const SearchResult& result : app.parking.search(textQuery)) {
        std::cout << "      " << result.loc.name << "\n";
    }

    std::cout << "\n[5] BOOKING + availability update\n";
    const ParkingLocation* target = app.parking.findById(1);
    int before = target ? target->availableSlots : 0;
    Booking booking;
    std::string error;
    if (app.bookings.book(1, 0, "Demo Student", 3, booking, error)) {
        std::cout << "      booked slot " << booking.slotLabel << " for " << booking.hours
                  << " h, amount Rs " << booking.amount << "\n";
        std::cout << "      free slots " << before << " -> "
                  << app.parking.findById(1)->availableSlots << "\n";
        Booking cancelled;
        Booking promoted;
        bool promotedFlag = false;
        if (app.bookings.cancel(booking.id, "Demo Student", cancelled, promotedFlag,
                                promoted, error)) {
            std::cout << "      cancelled -> free slots back to "
                      << app.parking.findById(1)->availableSlots << "\n";
        }
    } else {
        std::cout << "      booking failed: " << error << "\n";
    }

    std::cout << "\n[6] QUEUE - waiting list for a full lot\n";
    WaitEntry entry;
    bool already = false;
    if (app.bookings.joinWaitlist(9, "Queue Demo", entry, already, error)) {
        std::cout << "      " << app.parking.findById(9)->name << " waiting list size: "
                  << app.parking.waitingCount(9) << "\n";
    }
    WaitingQueue queue;
    queue.enqueue(WaitEntry{1, 9, "Asha", ""});
    queue.enqueue(WaitEntry{2, 9, "Ravi", ""});
    queue.enqueue(WaitEntry{3, 9, "Meera", ""});
    WaitEntry first;
    queue.dequeue(first);
    std::cout << "      FIFO demo: first promoted waiter is " << first.userName
              << " (" << queue.size() << " left)\n";

    std::cout << "\n[7] PRIORITY QUEUE is used inside Dijkstra (binary min-heap).\n";
    std::cout << "[8] LINKED LIST holds both the slot lists and active bookings ("
              << app.bookings.activeCount() << " active).\n";
    printSeparator();
    std::cout << "Total slots: " << app.parking.totalSlotsAll()
              << " | free: " << app.parking.availableSlotsAll() << "\n\n";
}

// ===========================================================================
//  main
// ===========================================================================
int main(int argc, char** argv) {
    int port = 8080;
    bool demo = false;
    std::string dbPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (arg == "--db" && i + 1 < argc) dbPath = argv[++i];
        else if (arg == "--demo") demo = true;
        else if (arg == "--help") {
            std::cout << "SmartParking C++ backend\n"
                         "  --port <n>   HTTP port (default 8080)\n"
                         "  --db <file>  SQLite file (default database/smartparking.db)\n"
                         "  --demo       print a console DSA demonstration and exit\n";
            return 0;
        }
    }

    std::string schemaPath = findPath({"database/schema.sql", "../database/schema.sql"}, argv[0]);
    if (!fs::exists(schemaPath)) {
        std::cerr << "schema.sql not found. Run the server from the project root.\n";
        return 1;
    }
    if (dbPath.empty()) {
        dbPath = (fs::path(schemaPath).parent_path() / "smartparking.db").string();
    }
    std::error_code ec;
    fs::create_directories(fs::path(dbPath).parent_path(), ec);

    App app;
    app.webRoot = findPath({"frontend", "../frontend"}, argv[0]);
    if (!app.db.open(dbPath)) {
        std::cerr << "Could not open database " << dbPath << "\n";
        return 1;
    }

    std::string error;
    if (!prepareData(app, schemaPath, error)) {
        std::cerr << "Startup failed: " << error << "\n";
        return 1;
    }

    if (demo) {
        runDemo(app);
        return 0;
    }

    HttpServer server([&app](const HttpRequest& request) { return dispatch(app, request); });
    if (!server.listenOn(port, error)) {
        std::cerr << "Server error: " << error << "\n";
        return 1;
    }

    std::cout << "SmartParking backend running\n";
    std::cout << "  URL      : http://localhost:" << port << "/\n";
    std::cout << "  frontend : " << app.webRoot << "\n";
    std::cout << "  database : " << dbPath << "\n";
    std::cout << "  parking  : " << app.parking.locations().size() << " locations, "
              << app.parking.totalSlotsAll() << " slots\n";
    std::cout << "  API      : /api/health, /api/locations, /api/stats, /api/bookings\n";
    std::cout << "Press Ctrl+C to stop.\n";
    server.run();
    return 0;
}
