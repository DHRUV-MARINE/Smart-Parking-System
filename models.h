// ============================================================================
//  models.h - Structures/classes that describe the SmartParking domain.
//  (DSA concept: STRUCTURES - plain records for parking lots, slots,
//   bookings, waiting-list entries and graph nodes.)
// ============================================================================
#pragma once

#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// A single parking location.
// ---------------------------------------------------------------------------
struct ParkingLocation {
    int id = 0;
    std::string name;
    std::string address;
    std::string area;
    double lat = 0.0;
    double lng = 0.0;
    int totalSlots = 0;
    int occupancySeed = 0;    // % occupied when slots are generated the first time
    int availableSlots = 0;   // rebuilt from the slot linked list at start-up
    double pricePerHour = 0.0;
    double price3Hours = 0.0;
    double pricePerDay = 0.0;
    std::string type;         // "Public" or "Private"
    int nodeId = -1;          // vertex of this lot in the road graph
    double rating = 0.0;
    std::string openHours;
};

// One physical parking slot.
struct Slot {
    int id = 0;
    int locationId = 0;
    std::string label;
    bool occupied = false;
};

// One booking record.
struct Booking {
    int id = 0;
    int locationId = 0;
    int slotId = 0;
    std::string slotLabel;
    std::string locationName;
    std::string userName;
    int hours = 1;
    double amount = 0.0;
    std::string status = "active";
    std::string createdAt;
};

// One waiting-list entry (someone waiting for a free slot).
struct WaitEntry {
    int id = 0;
    int locationId = 0;
    std::string userName;
    std::string createdAt;
};

// A raw row of the graph_edges table.
struct EdgeRow {
    int from = 0;
    int to = 0;
    double km = 0.0;
};

// A raw row of the graph_nodes table.
struct GraphNodeRow {
    int id = 0;
    std::string name;
    double lat = 0.0;
    double lng = 0.0;
    bool isLandmark = false;
};

// ---------------------------------------------------------------------------
// Tiered pricing: full days first, then the 3-hour package, then hourly.
//   hours = 26 -> 1 day + 2 hours   (daily + hourly)
//   hours = 5  -> 3h package + 2 hours
// ---------------------------------------------------------------------------
inline double costForHours(const ParkingLocation& loc, int hours) {
    if (hours < 1) hours = 1;

    int days = hours / 24;
    int rest = hours % 24;

    double total = days * loc.pricePerDay;
    if (rest >= 3) {
        // The 3-hour ticket is cheaper per hour; charge it for full 3h blocks.
        int blocks = rest / 3;
        total += blocks * loc.price3Hours;
        rest -= blocks * 3;
    }
    total += rest * loc.pricePerHour;
    return total;
}

// Traffic-light style availability label used by both backend and frontend.
inline std::string availabilityStatus(int available, int total) {
    if (available <= 0) return "Full";
    int limitedThreshold = std::max(3, total / 5);   // <=20% free means "Limited"
    if (available <= limitedThreshold) return "Limited";
    return "Available";
}
