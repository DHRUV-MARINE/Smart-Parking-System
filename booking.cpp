// ============================================================================
//  booking.cpp - Book / cancel / waiting-list logic.
// ============================================================================
#include "booking.h"

#include <cctype>

#include "database.h"
#include "parking.h"
#include "sorting.h"

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return text.substr(begin, end - begin);
}

}  // namespace

BookingManager::BookingManager(ParkingManager& parking, Database& db)
    : parking_(parking), db_(db) {}

BookingManager::~BookingManager() {
    Node* cur = head_;
    while (cur) {
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
}

bool BookingManager::load(std::string& error) {
    std::vector<Booking> rows;
    if (!db_.loadActiveBookings(rows)) {
        error = db_.lastError();
        return false;
    }
    for (const Booking& booking : rows) pushBack(booking);
    return true;
}

bool BookingManager::userMatches(const std::string& stored, const std::string& given) const {
    return toLower(trim(stored)) == toLower(trim(given));
}

void BookingManager::pushBack(const Booking& booking) {
    Node* node = new Node{booking, nullptr};
    if (!tail_) head_ = tail_ = node;
    else {
        tail_->next = node;   // O(1) append thanks to the tail pointer
        tail_ = node;
    }
    ++size_;
}

BookingManager::Node* BookingManager::findNode(int bookingId) const {
    for (Node* cur = head_; cur; cur = cur->next) {
        if (cur->data.id == bookingId) return cur;
    }
    return nullptr;
}

bool BookingManager::removeNode(int bookingId, Booking& removed) {
    Node* prev = nullptr;
    Node* cur = head_;
    while (cur) {
        if (cur->data.id == bookingId) {
            removed = cur->data;
            if (prev) prev->next = cur->next;
            else head_ = cur->next;
            if (cur == tail_) tail_ = prev;
            delete cur;
            --size_;
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Book a slot.
//   1. validate the request
//   2. take a free slot from the slot linked list (availability updates)
//   3. price the stay with the tiered price function
//   4. persist the booking and append it to the linked list
// ---------------------------------------------------------------------------
bool BookingManager::book(int locationId, int slotId, const std::string& userName,
                          int hours, Booking& out, std::string& error) {
    std::string user = trim(userName);
    if (user.empty()) {
        error = "please sign in with your name before booking";
        return false;
    }
    if (hours < 1 || hours > 168) {
        error = "duration must be between 1 and 168 hours";
        return false;
    }

    const ParkingLocation* loc = parking_.findById(locationId);
    if (!loc) {
        error = "parking location not found";
        return false;
    }

    Slot slot;
    if (!parking_.allocateSlot(locationId, slotId, slot, error)) return false;

    Booking booking;
    booking.locationId = locationId;
    booking.locationName = loc->name;
    booking.slotId = slot.id;
    booking.slotLabel = slot.label;
    booking.userName = user;
    booking.hours = hours;
    booking.amount = costForHours(*loc, hours);
    booking.status = "active";

    std::string createdAt;
    int id = db_.insertBooking(booking, createdAt, error);
    if (id <= 0) {
        parking_.releaseSlot(locationId, slot.id);   // roll back availability
        return false;
    }
    booking.id = id;
    booking.createdAt = createdAt;

    pushBack(booking);
    out = booking;
    return true;
}

// ---------------------------------------------------------------------------
// Cancel a booking.
//   1. find it in the linked list and check the owner
//   2. mark it cancelled in SQLite and free the slot
//   3. promote the first person from the waiting queue (FIFO), if any
// ---------------------------------------------------------------------------
bool BookingManager::cancel(int bookingId, const std::string& userName, Booking& out,
                            bool& promoted, Booking& promotedBooking, std::string& error) {
    promoted = false;
    Node* node = findNode(bookingId);
    if (!node) {
        error = "booking not found or already cancelled";
        return false;
    }
    if (!userMatches(node->data.userName, userName)) {
        error = "this booking belongs to a different user";
        return false;
    }

    Booking booking = node->data;
    if (!db_.updateBookingStatus(bookingId, "cancelled")) {
        error = db_.lastError();
        return false;
    }
    parking_.releaseSlot(booking.locationId, booking.slotId);
    booking.status = "cancelled";
    removeNode(bookingId, out);
    out = booking;

    // ---- queue promotion ---------------------------------------------------
    WaitingQueue* queue = parking_.queueFor(booking.locationId);
    if (queue && queue->size() > 0) {
        WaitEntry next;
        if (queue->dequeue(next)) {
            Booking promotedBookingLocal;
            std::string ignored;
            if (book(booking.locationId, 0, next.userName, 2, promotedBookingLocal, ignored)) {
                db_.removeWaiting(next.id);
                promoted = true;
                promotedBooking = promotedBookingLocal;
            } else {
                queue->enqueue(next);   // could not seat them - keep their place
            }
        }
    }
    return true;
}

bool BookingManager::joinWaitlist(int locationId, const std::string& userName,
                                  WaitEntry& out, bool& alreadyWaiting, std::string& error) {
    alreadyWaiting = false;
    const ParkingLocation* loc = parking_.findById(locationId);
    if (!loc) {
        error = "parking location not found";
        return false;
    }
    if (loc->availableSlots > 0) {
        error = "slots are available right now - you can book directly";
        return false;
    }
    std::string user = trim(userName);
    if (user.empty()) {
        error = "please sign in with your name first";
        return false;
    }

    WaitingQueue* queue = parking_.queueFor(locationId);
    if (!queue) {
        error = "waiting list unavailable";
        return false;
    }
    for (const WaitEntry& entry : queue->toVector()) {
        if (userMatches(entry.userName, user)) {
            alreadyWaiting = true;
            out = entry;
            return true;
        }
    }

    WaitEntry entry;
    entry.locationId = locationId;
    entry.userName = user;
    std::string createdAt;
    int id = db_.insertWaiting(locationId, user, createdAt, error);
    if (id <= 0) return false;
    entry.id = id;
    entry.createdAt = createdAt;
    queue->enqueue(entry);
    out = entry;
    return true;
}

std::vector<Booking> BookingManager::bookingsFor(const std::string& userName) const {
    std::vector<Booking> out;
    for (Node* cur = head_; cur; cur = cur->next) {
        if (userMatches(cur->data.userName, userName)) out.push_back(cur->data);
    }
    // Newest first (merge sort keeps it stable and shows the algorithm again).
    mergeSort(out, [](const Booking& a, const Booking& b) { return a.id > b.id; });
    return out;
}

std::vector<Booking> BookingManager::allActive() const {
    std::vector<Booking> out;
    for (Node* cur = head_; cur; cur = cur->next) out.push_back(cur->data);
    return out;
}

std::vector<WaitEntry> BookingManager::waitersFor(int locationId) const {
    const WaitingQueue* queue = parking_.queueFor(locationId);
    return queue ? queue->toVector() : std::vector<WaitEntry>{};
}

int BookingManager::activeCount() const { return size_; }

double BookingManager::revenue() const {
    double total = 0.0;
    for (Node* cur = head_; cur; cur = cur->next) total += cur->data.amount;
    return total;
}
