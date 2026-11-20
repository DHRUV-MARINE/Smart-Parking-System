// ============================================================================
//  database.cpp - Thin, beginner friendly wrapper around the SQLite C API.
// ============================================================================
#include "database.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "sqlite3.h"

namespace {

// Small RAII helper so statements are always finalised, even on error paths.
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK) {
            stmt_ = nullptr;
        }
    }
    ~Statement() {
        if (stmt_) sqlite3_finalize(stmt_);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const { return stmt_; }
    explicit operator bool() const { return stmt_ != nullptr; }

    void bindText(int index, const std::string& value) {
        sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bindInt(int index, int value) { sqlite3_bind_int(stmt_, index, value); }
    void bindDouble(int index, double value) { sqlite3_bind_double(stmt_, index, value); }

    bool step() { return sqlite3_step(stmt_) == SQLITE_ROW; }
    bool done() { return sqlite3_step(stmt_) == SQLITE_DONE; }

    std::string text(int column) const {
        const unsigned char* raw = sqlite3_column_text(stmt_, column);
        return raw ? reinterpret_cast<const char*>(raw) : "";
    }
    int integer(int column) const { return sqlite3_column_int(stmt_, column); }
    double number(int column) const { return sqlite3_column_double(stmt_, column); }

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

}  // namespace

Database::~Database() { close(); }

bool Database::open(const std::string& path) {
    close();
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 3000);
    exec("PRAGMA foreign_keys = ON;");
    return true;
}

void Database::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::exec(const std::string& sql) {
    if (!db_) return false;
    char* message = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
        lastError_ = message ? message : "SQL error";
        sqlite3_free(message);
        return false;
    }
    return true;
}

bool Database::runFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        lastError_ = "cannot read " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return exec(buffer.str());
}

bool Database::countRows(const std::string& table, int& count) {
    if (!db_) return false;
    Statement st(db_, "SELECT COUNT(*) FROM " + table + ";");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    if (!st.step()) return false;
    count = st.integer(0);
    return true;
}

bool Database::loadLocations(std::vector<ParkingLocation>& out) {
    Statement st(db_,
        "SELECT id, name, address, area, lat, lng, total_slots, occupancy_seed, price_per_hour, "
        "price_3_hours, price_per_day, type, node_id, rating, open_hours "
        "FROM parking_locations ORDER BY id;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        ParkingLocation loc;
        loc.id = st.integer(0);
        loc.name = st.text(1);
        loc.address = st.text(2);
        loc.area = st.text(3);
        loc.lat = st.number(4);
        loc.lng = st.number(5);
        loc.totalSlots = st.integer(6);
        loc.occupancySeed = st.integer(7);
        loc.pricePerHour = st.number(8);
        loc.price3Hours = st.number(9);
        loc.pricePerDay = st.number(10);
        loc.type = st.text(11);
        loc.nodeId = st.integer(12);
        loc.rating = st.number(13);
        loc.openHours = st.text(14);
        out.push_back(loc);
    }
    return true;
}

bool Database::loadNodes(std::vector<GraphNodeRow>& out) {
    Statement st(db_, "SELECT id, name, lat, lng, kind FROM graph_nodes ORDER BY id;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        GraphNodeRow row;
        row.id = st.integer(0);
        row.name = st.text(1);
        row.lat = st.number(2);
        row.lng = st.number(3);
        row.isLandmark = (st.text(4) == "landmark");
        out.push_back(row);
    }
    return true;
}

bool Database::loadEdges(std::vector<EdgeRow>& out) {
    Statement st(db_, "SELECT from_node, to_node, distance_km FROM graph_edges;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        EdgeRow row;
        row.from = st.integer(0);
        row.to = st.integer(1);
        row.km = st.number(2);
        out.push_back(row);
    }
    return true;
}

bool Database::loadSlots(std::vector<Slot>& out) {
    Statement st(db_, "SELECT id, location_id, label, occupied FROM slots ORDER BY id;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        Slot slot;
        slot.id = st.integer(0);
        slot.locationId = st.integer(1);
        slot.label = st.text(2);
        slot.occupied = st.integer(3) != 0;
        out.push_back(slot);
    }
    return true;
}

bool Database::loadActiveBookings(std::vector<Booking>& out) {
    Statement st(db_,
        "SELECT b.id, b.location_id, b.slot_id, s.label, p.name, b.user_name, "
        "b.hours, b.amount, b.status, b.created_at "
        "FROM bookings b "
        "JOIN slots s ON s.id = b.slot_id "
        "JOIN parking_locations p ON p.id = b.location_id "
        "WHERE b.status = 'active' ORDER BY b.id;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        Booking booking;
        booking.id = st.integer(0);
        booking.locationId = st.integer(1);
        booking.slotId = st.integer(2);
        booking.slotLabel = st.text(3);
        booking.locationName = st.text(4);
        booking.userName = st.text(5);
        booking.hours = st.integer(6);
        booking.amount = st.number(7);
        booking.status = st.text(8);
        booking.createdAt = st.text(9);
        out.push_back(booking);
    }
    return true;
}

bool Database::loadWaiting(std::vector<WaitEntry>& out) {
    Statement st(db_, "SELECT id, location_id, user_name, created_at FROM waiting_list ORDER BY id;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    out.clear();
    while (st.step()) {
        WaitEntry entry;
        entry.id = st.integer(0);
        entry.locationId = st.integer(1);
        entry.userName = st.text(2);
        entry.createdAt = st.text(3);
        out.push_back(entry);
    }
    return true;
}

bool Database::insertSlot(int locationId, const std::string& label, bool occupied, int& newId) {
    Statement st(db_, "INSERT INTO slots (location_id, label, occupied) VALUES (?, ?, ?);");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    st.bindInt(1, locationId);
    st.bindText(2, label);
    st.bindInt(3, occupied ? 1 : 0);
    if (!st.done()) { lastError_ = sqlite3_errmsg(db_); return false; }
    newId = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return true;
}

bool Database::setSlotOccupied(int slotId, bool occupied) {
    Statement st(db_, "UPDATE slots SET occupied = ? WHERE id = ?;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    st.bindInt(1, occupied ? 1 : 0);
    st.bindInt(2, slotId);
    if (!st.done()) { lastError_ = sqlite3_errmsg(db_); return false; }
    return true;
}

int Database::insertBooking(const Booking& booking, std::string& createdAt, std::string& error) {
    Statement st(db_,
        "INSERT INTO bookings (location_id, slot_id, user_name, hours, amount, status) "
        "VALUES (?, ?, ?, ?, ?, 'active') RETURNING id, created_at;");
    if (!st) { error = sqlite3_errmsg(db_); return 0; }
    st.bindInt(1, booking.locationId);
    st.bindInt(2, booking.slotId);
    st.bindText(3, booking.userName);
    st.bindInt(4, booking.hours);
    st.bindDouble(5, booking.amount);
    if (!st.step()) { error = sqlite3_errmsg(db_); return 0; }
    createdAt = st.text(1);
    return st.integer(0);
}

bool Database::updateBookingStatus(int bookingId, const std::string& status) {
    Statement st(db_, "UPDATE bookings SET status = ? WHERE id = ?;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    st.bindText(1, status);
    st.bindInt(2, bookingId);
    if (!st.done()) { lastError_ = sqlite3_errmsg(db_); return false; }
    return true;
}

int Database::insertWaiting(int locationId, const std::string& userName,
                           std::string& createdAt, std::string& error) {
    Statement st(db_,
        "INSERT INTO waiting_list (location_id, user_name) VALUES (?, ?) RETURNING id, created_at;");
    if (!st) { error = sqlite3_errmsg(db_); return 0; }
    st.bindInt(1, locationId);
    st.bindText(2, userName);
    if (!st.step()) { error = sqlite3_errmsg(db_); return 0; }
    createdAt = st.text(1);
    return st.integer(0);
}

bool Database::removeWaiting(int waitingId) {
    Statement st(db_, "DELETE FROM waiting_list WHERE id = ?;");
    if (!st) { lastError_ = sqlite3_errmsg(db_); return false; }
    st.bindInt(1, waitingId);
    if (!st.done()) { lastError_ = sqlite3_errmsg(db_); return false; }
    return true;
}
