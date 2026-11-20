// ============================================================================
//  parking.cpp - Slot linked lists, waiting queues, search / sort / filter.
// ============================================================================
#include "parking.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "database.h"
#include "sorting.h"

// ===========================================================================
//  SlotList - singly linked list of slots
// ===========================================================================
SlotList::~SlotList() {
    SlotNode* cur = head_;
    while (cur) {
        SlotNode* next = cur->next;
        delete cur;
        cur = next;
    }
}

void SlotList::append(int id, const std::string& label, bool occupied) {
    SlotNode* node = new SlotNode{id, label, occupied, nullptr};
    if (!head_) head_ = tail_ = node;
    else {
        tail_->next = node;   // O(1) because we keep a tail pointer
        tail_ = node;
    }
    ++size_;
}

int SlotList::countFree() const {
    int free = 0;
    for (SlotNode* cur = head_; cur; cur = cur->next) {
        if (!cur->occupied) ++free;
    }
    return free;
}

int SlotList::firstFreeId() const {
    for (SlotNode* cur = head_; cur; cur = cur->next) {
        if (!cur->occupied) return cur->id;
    }
    return -1;
}

bool SlotList::setOccupied(int slotId, bool occupied) {
    for (SlotNode* cur = head_; cur; cur = cur->next) {
        if (cur->id == slotId) {
            cur->occupied = occupied;
            return true;
        }
    }
    return false;
}

std::vector<Slot> SlotList::toVector(int locationId) const {
    std::vector<Slot> out;
    out.reserve(static_cast<std::size_t>(size_));
    for (SlotNode* cur = head_; cur; cur = cur->next) {
        out.push_back(Slot{cur->id, locationId, cur->label, cur->occupied});
    }
    return out;
}

// ===========================================================================
//  WaitingQueue - FIFO queue of people waiting for a slot
// ===========================================================================
WaitingQueue::~WaitingQueue() {
    WaitNode* cur = front_;
    while (cur) {
        WaitNode* next = cur->next;
        delete cur;
        cur = next;
    }
}

void WaitingQueue::enqueue(const WaitEntry& entry) {
    WaitNode* node = new WaitNode{entry, nullptr};
    if (!front_) front_ = back_ = node;
    else {
        back_->next = node;
        back_ = node;
    }
    ++size_;
}

bool WaitingQueue::dequeue(WaitEntry& out) {
    if (!front_) return false;
    WaitNode* node = front_;
    out = node->data;
    front_ = node->next;
    if (!front_) back_ = nullptr;
    delete node;
    --size_;
    return true;
}

std::vector<WaitEntry> WaitingQueue::toVector() const {
    std::vector<WaitEntry> out;
    for (WaitNode* cur = front_; cur; cur = cur->next) out.push_back(cur->data);
    return out;
}

// ===========================================================================
//  ParkingManager - loading
// ===========================================================================
bool ParkingManager::load(Database& db, std::string& error) {
    if (!db.loadLocations(locations_)) {
        error = db.lastError();
        return false;
    }
    if (locations_.empty()) {
        error = "no parking locations found";
        return false;
    }

    slots_.clear();
    queues_.clear();
    locNodeIndex_.assign(locations_.size(), -1);

    for (std::size_t i = 0; i < locations_.size(); ++i) {
        idIndex_.insert(locations_[i].id, static_cast<int>(i));
        slots_.push_back(std::unique_ptr<SlotList>(new SlotList()));
        queues_.push_back(std::unique_ptr<WaitingQueue>(new WaitingQueue()));
    }
    rebuildNameIndex();

    // ---- graph ------------------------------------------------------------
    std::vector<GraphNodeRow> nodes;
    if (!db.loadNodes(nodes)) {
        error = db.lastError();
        return false;
    }
    int maxNodeId = 0;
    for (const GraphNodeRow& node : nodes) maxNodeId = std::max(maxNodeId, node.id);

    std::vector<int> nodeIndex(static_cast<std::size_t>(maxNodeId) + 1, -1);
    for (const GraphNodeRow& node : nodes) {
        int index = graph_.addNode();
        nodeIndex[static_cast<std::size_t>(node.id)] = index;
        nodeNames_.push_back(node.name);
        nodeLat_.push_back(node.lat);
        nodeLng_.push_back(node.lng);
        if (node.isLandmark) {
            origins_.push_back(OriginOption{node.name, index, node.lat, node.lng});
        }
    }

    // Map each parking lot to its graph vertex.
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        int nodeId = locations_[i].nodeId;
        if (nodeId >= 0 && nodeId <= maxNodeId) {
            locNodeIndex_[i] = nodeIndex[static_cast<std::size_t>(nodeId)];
        }
    }

    std::vector<EdgeRow> edges;
    if (!db.loadEdges(edges)) {
        error = db.lastError();
        return false;
    }
    for (const EdgeRow& row : edges) {
        if (row.from < 0 || row.to < 0) continue;
        if (row.from > maxNodeId || row.to > maxNodeId) continue;
        int u = nodeIndex[static_cast<std::size_t>(row.from)];
        int v = nodeIndex[static_cast<std::size_t>(row.to)];
        if (u < 0 || v < 0) continue;
        graph_.addEdge(u, v, row.km, false);   // both directions come from SQL
    }

    // ---- slots ------------------------------------------------------------
    std::vector<Slot> rows;
    if (!db.loadSlots(rows)) {
        error = db.lastError();
        return false;
    }
    for (const Slot& slot : rows) {
        std::string lookupError;
        int pos = positionOf(slot.locationId, lookupError);
        if (pos >= 0) slots_[static_cast<std::size_t>(pos)]->append(slot.id, slot.label, slot.occupied);
    }

    // ---- waiting list -----------------------------------------------------
    std::vector<WaitEntry> waiters;
    if (!db.loadWaiting(waiters)) {
        error = db.lastError();
        return false;
    }
    for (const WaitEntry& entry : waiters) {
        std::string lookupError;
        int pos = positionOf(entry.locationId, lookupError);
        if (pos >= 0) queues_[static_cast<std::size_t>(pos)]->enqueue(entry);
    }

    for (std::size_t i = 0; i < locations_.size(); ++i) syncAvailability(static_cast<int>(i));
    return true;
}

void ParkingManager::rebuildNameIndex() {
    sortedNames_.clear();
    sortedNamePositions_.clear();
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        sortedNames_.push_back(toLower(locations_[i].name));
        sortedNamePositions_.push_back(static_cast<int>(i));
    }

    // Keep names and positions together while sorting by name (merge sort).
    struct Pair { std::string name; int pos; };
    std::vector<Pair> pairs;
    for (std::size_t i = 0; i < sortedNames_.size(); ++i) {
        pairs.push_back(Pair{sortedNames_[i], sortedNamePositions_[i]});
    }
    mergeSort(pairs, [](const Pair& a, const Pair& b) { return a.name < b.name; });
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        sortedNames_[i] = pairs[i].name;
        sortedNamePositions_[i] = pairs[i].pos;
    }
}

void ParkingManager::syncAvailability(int position) {
    ParkingLocation& loc = locations_[static_cast<std::size_t>(position)];
    loc.availableSlots = slots_[static_cast<std::size_t>(position)]->countFree();
}

int ParkingManager::positionOf(int locationId, std::string& error) const {
    int pos = -1;
    if (!idIndex_.find(locationId, pos)) {
        error = "parking location " + std::to_string(locationId) + " not found";
        return -1;
    }
    return pos;
}

const ParkingLocation* ParkingManager::findById(int id) const {
    int pos = -1;
    if (!idIndex_.find(id, pos)) return nullptr;
    return &locations_[static_cast<std::size_t>(pos)];
}

int ParkingManager::indexOfId(int id) const {
    int pos = -1;
    return idIndex_.find(id, pos) ? pos : -1;
}

// ===========================================================================
//  Origin handling
// ===========================================================================
int ParkingManager::nodeIndexForPlace(const std::string& place) const {
    std::string needle = toLower(place);
    if (needle.empty()) return -1;

    for (const OriginOption& origin : origins_) {
        if (toLower(origin.name) == needle) return origin.nodeIndex;
    }
    for (const OriginOption& origin : origins_) {
        if (toLower(origin.name).find(needle) != std::string::npos) return origin.nodeIndex;
    }
    // Also allow "Sanjauli", "Kufri", ... (lot areas).
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        if (toLower(locations_[i].area).find(needle) != std::string::npos) {
            return locNodeIndex_[i];
        }
    }
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        if (toLower(locations_[i].name).find(needle) != std::string::npos) {
            return locNodeIndex_[i];
        }
    }
    return -1;
}

int ParkingManager::nearestNodeIndex(double lat, double lng) const {
    int best = -1;
    double bestKm = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < nodeNames_.size(); ++i) {
        double km = haversineKm(lat, lng, nodeLat_[i], nodeLng_[i]);
        if (km < bestKm) {
            bestKm = km;
            best = static_cast<int>(i);
        }
    }
    return best;
}

std::string ParkingManager::nodeName(int nodeIndex) const {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodeNames_.size())) return "";
    return nodeNames_[static_cast<std::size_t>(nodeIndex)];
}

bool ParkingManager::routeTo(int originNodeIndex, int locationId,
                             std::vector<std::string>& names, double& km) const {
    names.clear();
    km = 0.0;
    int pos = indexOfId(locationId);
    if (pos < 0) return false;

    int target = locNodeIndex_[static_cast<std::size_t>(pos)];
    if (target < 0) return false;

    // One Dijkstra run gives both the distance and the parent pointers needed
    // to rebuild the route, so no second search is required.
    std::vector<int> parent;
    std::vector<double> dist = graph_.dijkstra(originNodeIndex, &parent);
    if (!std::isfinite(dist[static_cast<std::size_t>(target)])) return false;

    km = dist[static_cast<std::size_t>(target)];
    for (int at = target; at != -1; at = parent[static_cast<std::size_t>(at)]) {
        names.push_back(nodeName(at));
        if (at == originNodeIndex) break;
    }
    std::reverse(names.begin(), names.end());
    return !names.empty();
}

// ===========================================================================
//  Search + filter + sort
// ===========================================================================
std::vector<SearchResult> ParkingManager::search(const SearchQuery& query) const {
    // ---- 1. Candidate selection (searching algorithms) --------------------
    std::vector<int> candidates;
    const std::string needle = toLower(query.text);

    if (needle.empty()) {
        for (std::size_t i = 0; i < locations_.size(); ++i) candidates.push_back(static_cast<int>(i));
    } else {
        // (a) Binary search for an exact name match - O(log n).
        int exact = binarySearchName(sortedNames_, needle);
        if (exact >= 0) candidates.push_back(sortedNamePositions_[static_cast<std::size_t>(exact)]);

        // (b) Lower bound + forward scan finds every prefix match - O(log n + k).
        int start = lowerBoundName(sortedNames_, needle);
        for (int i = start; i < static_cast<int>(sortedNames_.size()); ++i) {
            if (sortedNames_[static_cast<std::size_t>(i)].compare(0, needle.size(), needle) != 0) break;
            int pos = sortedNamePositions_[static_cast<std::size_t>(i)];
            if (std::find(candidates.begin(), candidates.end(), pos) == candidates.end()) {
                candidates.push_back(pos);
            }
        }

        // (c) Substring scan over name / area / address for everything else.
        for (std::size_t i = 0; i < locations_.size(); ++i) {
            const ParkingLocation& loc = locations_[i];
            if (!containsIgnoreCase(loc.name, needle) &&
                !containsIgnoreCase(loc.area, needle) &&
                !containsIgnoreCase(loc.address, needle)) {
                continue;
            }
            int pos = static_cast<int>(i);
            if (std::find(candidates.begin(), candidates.end(), pos) == candidates.end()) {
                candidates.push_back(pos);
            }
        }
    }

    // ---- 2. Distances via Dijkstra (one run, reused for every lot) --------
    std::vector<double> dist;
    int originNode = -1;
    if (!query.place.empty()) originNode = nodeIndexForPlace(query.place);
    if (originNode < 0 && query.hasCoords) originNode = nearestNodeIndex(query.lat, query.lng);

    bool haveGraphDistance = false;
    if (originNode >= 0) {
        dist = graph_.dijkstra(originNode);
        haveGraphDistance = true;
    }

    // ---- 3. Filters -------------------------------------------------------
    std::vector<SearchResult> results;
    for (int pos : candidates) {
        const ParkingLocation& loc = locations_[static_cast<std::size_t>(pos)];
        if (!query.includeFull && loc.availableSlots <= 0) continue;
        if (query.maxPrice > 0 && loc.pricePerHour > query.maxPrice) continue;
        if (query.minAvailable >= 0 && loc.availableSlots < query.minAvailable) continue;
        if (!query.type.empty() && loc.type != query.type) continue;

        SearchResult result;
        result.loc = loc;

        if (haveGraphDistance) {
            int node = locNodeIndex_[static_cast<std::size_t>(pos)];
            if (node >= 0 && std::isfinite(dist[static_cast<std::size_t>(node)])) {
                result.distanceKm = dist[static_cast<std::size_t>(node)];
                result.reachable = true;
            }
        } else if (query.hasCoords) {
            result.distanceKm = haversineKm(query.lat, query.lng, loc.lat, loc.lng);
            result.reachable = true;
        }

        if (query.maxDistanceKm > 0 && (!result.reachable || result.distanceKm > query.maxDistanceKm)) {
            continue;
        }
        results.push_back(result);
    }

    // ---- 4. Sorting (manual algorithms) -----------------------------------
    if (query.sortBy == "price") {
        // Quick sort: cheap hourly price first, distance breaks ties.
        quickSort(results, [](const SearchResult& a, const SearchResult& b) {
            if (a.loc.pricePerHour != b.loc.pricePerHour)
                return a.loc.pricePerHour < b.loc.pricePerHour;
            return a.distanceKm < b.distanceKm;
        });
    } else if (query.sortBy == "availability") {
        // Merge sort (stable): most free slots first.
        mergeSort(results, [](const SearchResult& a, const SearchResult& b) {
            if (a.loc.availableSlots != b.loc.availableSlots)
                return a.loc.availableSlots > b.loc.availableSlots;
            return a.loc.pricePerHour < b.loc.pricePerHour;
        });
    } else if (query.sortBy == "name") {
        mergeSort(results, [](const SearchResult& a, const SearchResult& b) {
            return toLower(a.loc.name) < toLower(b.loc.name);
        });
    } else {
        // Merge sort (stable) by shortest road distance; unreachable at the end.
        mergeSort(results, [](const SearchResult& a, const SearchResult& b) {
            if (a.reachable != b.reachable) return a.reachable;
            if (a.distanceKm != b.distanceKm) return a.distanceKm < b.distanceKm;
            return a.loc.pricePerHour < b.loc.pricePerHour;
        });
    }

    if (query.limit > 0 && static_cast<int>(results.size()) > query.limit) {
        results.resize(static_cast<std::size_t>(query.limit));
    }
    return results;
}

// ===========================================================================
//  Slots / availability
// ===========================================================================
std::vector<Slot> ParkingManager::slotsOf(int locationId) const {
    int pos = indexOfId(locationId);
    if (pos < 0) return {};
    return slots_[static_cast<std::size_t>(pos)]->toVector(locationId);
}

bool ParkingManager::allocateSlot(int locationId, int slotId, Slot& booked, std::string& error) {
    int pos = positionOf(locationId, error);
    if (pos < 0) return false;

    SlotList& list = *slots_[static_cast<std::size_t>(pos)];
    int chosenId = slotId;
    if (chosenId <= 0) {
        chosenId = list.firstFreeId();
        if (chosenId < 0) {
            error = "this parking lot is full";
            return false;
        }
    }

    // Check the slot is free before taking it.
    bool found = false;
    bool occupied = true;
    for (const Slot& slot : list.toVector(locationId)) {
        if (slot.id == chosenId) {
            found = true;
            occupied = slot.occupied;
            booked = slot;
            break;
        }
    }
    if (!found) {
        error = "slot does not belong to this parking lot";
        return false;
    }
    if (occupied) {
        error = "that slot is already occupied";
        return false;
    }

    list.setOccupied(chosenId, true);
    syncAvailability(pos);
    booked.occupied = true;
    return true;
}

bool ParkingManager::releaseSlot(int locationId, int slotId) {
    int pos = indexOfId(locationId);
    if (pos < 0) return false;
    bool ok = slots_[static_cast<std::size_t>(pos)]->setOccupied(slotId, false);
    if (ok) syncAvailability(pos);
    return ok;
}

WaitingQueue* ParkingManager::queueFor(int locationId) {
    int pos = indexOfId(locationId);
    if (pos < 0) return nullptr;
    return queues_[static_cast<std::size_t>(pos)].get();
}

const WaitingQueue* ParkingManager::queueFor(int locationId) const {
    int pos = indexOfId(locationId);
    if (pos < 0) return nullptr;
    return queues_[static_cast<std::size_t>(pos)].get();
}

int ParkingManager::waitingCount(int locationId) const {
    const WaitingQueue* queue = queueFor(locationId);
    return queue ? queue->size() : 0;
}

int ParkingManager::totalSlotsAll() const {
    int total = 0;
    for (const ParkingLocation& loc : locations_) total += loc.totalSlots;
    return total;
}

int ParkingManager::availableSlotsAll() const {
    int total = 0;
    for (const ParkingLocation& loc : locations_) total += loc.availableSlots;
    return total;
}
