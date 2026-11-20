// ============================================================================
//  database.h - SQLite persistence layer.
//  Stores every parking lot, slot, booking and waiting-list entry so the
//  data survives a restart (requirement 7: persistent storage).
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "models.h"

struct sqlite3;

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool open(const std::string& path);
    void close();
    bool isOpen() const { return db_ != nullptr; }

    // Runs a whole .sql script (multiple statements).
    bool runFile(const std::string& path);
    bool exec(const std::string& sql);

    std::string lastError() const { return lastError_; }

    // ---- readers -----------------------------------------------------------
    bool countRows(const std::string& table, int& count);
    bool loadLocations(std::vector<ParkingLocation>& out);
    bool loadNodes(std::vector<GraphNodeRow>& out);
    bool loadEdges(std::vector<EdgeRow>& out);
    bool loadSlots(std::vector<Slot>& out);
    bool loadActiveBookings(std::vector<Booking>& out);
    bool loadWaiting(std::vector<WaitEntry>& out);

    // ---- writers -----------------------------------------------------------
    bool insertSlot(int locationId, const std::string& label, bool occupied, int& newId);
    bool setSlotOccupied(int slotId, bool occupied);
    int insertBooking(const Booking& booking, std::string& createdAt, std::string& error);
    bool updateBookingStatus(int bookingId, const std::string& status);
    int insertWaiting(int locationId, const std::string& userName, std::string& createdAt,
                      std::string& error);
    bool removeWaiting(int waitingId);

private:
    sqlite3* db_ = nullptr;
    std::string lastError_;
};
