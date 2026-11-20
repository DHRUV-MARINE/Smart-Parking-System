// ============================================================================
//  booking.h - Booking and cancellation logic.
//  (DSA concept: LINKED LIST of active bookings, QUEUE promotion from the
//   waiting list when a slot is released.)
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "models.h"

class Database;
class ParkingManager;

class BookingManager {
public:
    BookingManager(ParkingManager& parking, Database& db);

    BookingManager(const BookingManager&) = delete;
    BookingManager& operator=(const BookingManager&) = delete;
    ~BookingManager();

    bool load(std::string& error);

    // Books a slot (slotId == 0 -> first free slot). Updates availability.
    bool book(int locationId, int slotId, const std::string& userName, int hours,
              Booking& out, std::string& error);

    // Cancels a booking, frees the slot and promotes the first waiter, if any.
    bool cancel(int bookingId, const std::string& userName, Booking& out,
                bool& promoted, Booking& promotedBooking, std::string& error);

    // Adds a user to the waiting list of a full lot.
    bool joinWaitlist(int locationId, const std::string& userName,
                      WaitEntry& out, bool& alreadyWaiting, std::string& error);

    std::vector<Booking> bookingsFor(const std::string& userName) const;
    std::vector<Booking> allActive() const;
    std::vector<WaitEntry> waitersFor(int locationId) const;

    int activeCount() const;
    double revenue() const;

private:
    struct Node {
        Booking data;
        Node* next = nullptr;
    };

    Node* findNode(int bookingId) const;
    void pushBack(const Booking& booking);
    bool removeNode(int bookingId, Booking& removed);
    bool userMatches(const std::string& stored, const std::string& given) const;

    ParkingManager& parking_;
    Database& db_;
    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    int size_ = 0;
};
