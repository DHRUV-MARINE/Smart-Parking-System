// ============================================================================
//  parking.h - The parking engine: slots, availability, search, sorting,
//              filtering and the road-graph origin handling.
//
//  DSA concepts in this file:
//    * LINKED LIST   - SlotList holds every slot of a lot in a singly linked
//                      list; WaitingQueue is a FIFO queue of waiters.
//    * HASH TABLE    - id -> vector position, name -> vector position.
//    * GRAPH         - road network (see graph.h).
//    * DIJKSTRA      - distance from the user's origin to every lot.
//    * SEARCHING     - binary search (exact name) + lower bound (prefix) +
//                      substring scan for area / address.
//    * SORTING       - merge sort and quick sort, written by hand.
// ============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "graph.h"
#include "hash_table.h"
#include "models.h"

class Database;

// ---------------------------------------------------------------------------
// One node of the per-lot slot linked list.
// ---------------------------------------------------------------------------
struct SlotNode {
    int id = 0;
    std::string label;
    bool occupied = false;
    SlotNode* next = nullptr;
};

// Singly linked list of all slots of one parking location.
class SlotList {
public:
    SlotList() = default;
    ~SlotList();

    SlotList(const SlotList&) = delete;
    SlotList& operator=(const SlotList&) = delete;

    void append(int id, const std::string& label, bool occupied);
    int size() const { return size_; }
    int countFree() const;
    int firstFreeId() const;                                  // -1 if full
    bool setOccupied(int slotId, bool occupied);
    std::vector<Slot> toVector(int locationId) const;

private:
    SlotNode* head_ = nullptr;
    SlotNode* tail_ = nullptr;
    int size_ = 0;
};

// ---------------------------------------------------------------------------
// FIFO queue used as the waiting list of a full parking lot.
// ---------------------------------------------------------------------------
struct WaitNode {
    WaitEntry data;
    WaitNode* next = nullptr;
};

class WaitingQueue {
public:
    WaitingQueue() = default;
    ~WaitingQueue();

    WaitingQueue(const WaitingQueue&) = delete;
    WaitingQueue& operator=(const WaitingQueue&) = delete;

    void enqueue(const WaitEntry& entry);   // O(1) - keeps a tail pointer
    bool dequeue(WaitEntry& out);           // O(1)
    int size() const { return size_; }
    std::vector<WaitEntry> toVector() const;

private:
    WaitNode* front_ = nullptr;
    WaitNode* back_ = nullptr;
    int size_ = 0;
};

// A place the user can start from, already mapped to a graph vertex.
struct OriginOption {
    std::string name;
    int nodeIndex = -1;
    double lat = 0.0;
    double lng = 0.0;
};

// Everything the frontend can ask for in one search request.
struct SearchQuery {
    std::string text;            // matches name / area / address
    std::string place;           // starting landmark name (optional)
    bool hasCoords = false;      // browser geolocation (optional)
    double lat = 0.0;
    double lng = 0.0;
    std::string sortBy = "distance";   // distance | price | availability | name
    double maxPrice = 0.0;             // 0 = no limit
    double maxDistanceKm = 0.0;        // 0 = no limit
    int minAvailable = -1;             // -1 = no limit
    std::string type;                  // "Public" / "Private" / ""
    bool includeFull = true;           // show lots with 0 free slots
    int limit = 0;                     // 0 = no limit
};

struct SearchResult {
    ParkingLocation loc;
    double distanceKm = 0.0;
    bool reachable = false;
};

// ---------------------------------------------------------------------------
// ParkingManager owns every lot, its slots, its waiting queue and the graph.
// ---------------------------------------------------------------------------
class ParkingManager {
public:
    ParkingManager() = default;
    ParkingManager(const ParkingManager&) = delete;
    ParkingManager& operator=(const ParkingManager&) = delete;

    bool load(Database& db, std::string& error);

    const std::vector<ParkingLocation>& locations() const { return locations_; }
    const std::vector<OriginOption>& origins() const { return origins_; }
    const Graph& graph() const { return graph_; }

    const ParkingLocation* findById(int id) const;   // hash table lookup

    // ---- origin / distance helpers ----------------------------------------
    int nodeIndexForPlace(const std::string& place) const;
    int nearestNodeIndex(double lat, double lng) const;
    std::string nodeName(int nodeIndex) const;

    // Full shortest route (node names) from an origin vertex to a lot.
    bool routeTo(int originNodeIndex, int locationId,
                 std::vector<std::string>& names, double& km) const;

    // ---- search ------------------------------------------------------------
    std::vector<SearchResult> search(const SearchQuery& query) const;

    // ---- slots & availability ---------------------------------------------
    std::vector<Slot> slotsOf(int locationId) const;

    // Books a slot: slotId == 0 means "first free slot". Updates availability.
    bool allocateSlot(int locationId, int slotId, Slot& booked, std::string& error);
    bool releaseSlot(int locationId, int slotId);

    // ---- waiting list (queue) ---------------------------------------------
    WaitingQueue* queueFor(int locationId);
    const WaitingQueue* queueFor(int locationId) const;
    int waitingCount(int locationId) const;

    // ---- aggregates for the dashboard -------------------------------------
    int totalSlotsAll() const;
    int availableSlotsAll() const;

private:
    int indexOfId(int id) const;
    int positionOf(int locationId, std::string& error) const;
    void rebuildNameIndex();
    void syncAvailability(int position);

    std::vector<ParkingLocation> locations_;
    std::vector<std::unique_ptr<SlotList>> slots_;      // parallel to locations_
    std::vector<std::unique_ptr<WaitingQueue>> queues_; // parallel to locations_
    std::vector<int> locNodeIndex_;                     // parallel to locations_

    HashTable<int, int, IntHasher> idIndex_{31};
    std::vector<std::string> sortedNames_;   // lowercase names, sorted
    std::vector<int> sortedNamePositions_;   // matching positions

    Graph graph_;
    std::vector<OriginOption> origins_;
    std::vector<std::string> nodeNames_;
    std::vector<double> nodeLat_;
    std::vector<double> nodeLng_;
};
