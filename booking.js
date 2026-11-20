/* ==========================================================================
   Booking confirmation + "my bookings" management.
   ========================================================================== */
(function () {
    'use strict';

    const params = new URLSearchParams(window.location.search);
    const reference = params.get('ref');
    const confirmation = document.getElementById('confirmation');
    const wrap = document.getElementById('bookings-wrap');
    const subtitle = document.getElementById('bookings-subtitle');

    function confirmationCard(data) {
        const booking = data.booking;
        const loc = data.location;
        return '' +
            '<div class="confirm-card">' +
            '<div class="confirm-head">' +
            '<div class="check">' +
            '<svg width="26" height="26" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="M20 6L9 17l-5-5"></path></svg>' +
            '</div><div>' +
            '<h1>Booking confirmed</h1>' +
            '<p>Slot ' + SP.escape(booking.slotLabel) + ' is reserved at ' + SP.escape(booking.locationName) +
            ' for ' + booking.hours + ' hour' + (booking.hours === 1 ? '' : 's') + '.</p>' +
            '</div></div>' +
            '<div class="confirm-body">' +
            '<div class="receipt">' +
            '<div class="cell"><span>Booking reference</span><strong>#' + booking.id + '</strong><small>Show this at the entry gate</small></div>' +
            '<div class="cell"><span>Slot</span><strong>' + SP.escape(booking.slotLabel) + '</strong><small>' + SP.escape(booking.locationName) + '</small></div>' +
            '<div class="cell"><span>Duration</span><strong>' + booking.hours + ' hours</strong><small>From now</small></div>' +
            '<div class="cell"><span>Amount payable</span><strong>' + SP.money(booking.amount) + '</strong><small>Includes tiered long-stay pricing</small></div>' +
            '<div class="cell"><span>Booked for</span><strong>' + SP.escape(booking.userName) + '</strong></div>' +
            '<div class="cell"><span>Parking address</span><strong style="font-size:13.5px">' +
            (loc ? SP.escape(loc.address) : '—') + '</strong></div>' +
            '</div>' +
            '<div class="row mt-3 wrap">' +
            (loc ? '<a class="btn btn-secondary" href="/parking.html?id=' + loc.id + '">View parking details</a>' : '') +
            '<a class="btn btn-primary" href="/find.html">Book another slot</a>' +
            '<button class="btn btn-danger" type="button" id="cancel-from-confirm" data-booking="' + booking.id + '">Cancel this booking</button>' +
            '</div>' +
            (loc ? '<p class="hint mt-2">' + loc.availableSlots + ' of ' + loc.totalSlots + ' slots are free right now.</p>' : '') +
            '</div></div>';
    }

    async function loadConfirmation() {
        if (!reference) return;
        try {
            const data = await SP.api('/bookings/' + encodeURIComponent(reference));
            confirmation.innerHTML = confirmationCard(data);
            const cancel = document.getElementById('cancel-from-confirm');
            if (cancel) {
                cancel.addEventListener('click', function () {
                    cancelBooking(Number(cancel.getAttribute('data-booking')));
                });
            }
        } catch (error) {
            confirmation.innerHTML = '<div class="empty-state"><h3>Booking not found</h3><p>' +
                SP.escape(error.message) + '</p><a class="btn btn-primary" href="/find.html">Find parking</a></div>';
        }
    }

    function renderBookings(list) {
        if (!list.length) {
            wrap.innerHTML = '<div class="empty-state"><h3>No active bookings</h3>' +
                '<p>When you reserve a slot it will appear here with its slot number and price.</p>' +
                '<a class="btn btn-primary" href="/find.html">Find parking</a></div>';
            return;
        }
        wrap.innerHTML = '<table class="table"><thead><tr>' +
            '<th>Ref</th><th>Parking</th><th>Slot</th><th>Duration</th><th>Amount</th><th>Status</th><th></th>' +
            '</tr></thead><tbody>' +
            list.map(function (booking) {
                return '<tr>' +
                    '<td><strong>#' + booking.id + '</strong></td>' +
                    '<td class="name-cell"><a href="/parking.html?id=' + booking.locationId + '">' +
                    SP.escape(booking.locationName) + '</a></td>' +
                    '<td>' + SP.escape(booking.slotLabel) + '</td>' +
                    '<td>' + booking.hours + ' h</td>' +
                    '<td>' + SP.money(booking.amount) + '</td>' +
                    '<td><span class="badge badge-available"><i></i>Active</span></td>' +
                    '<td><button class="btn btn-danger btn-sm" type="button" data-cancel="' + booking.id + '">Cancel</button></td>' +
                    '</tr>';
            }).join('') + '</tbody></table>';

        wrap.querySelectorAll('[data-cancel]').forEach(function (button) {
            button.addEventListener('click', function () {
                cancelBooking(Number(button.getAttribute('data-cancel')));
            });
        });
    }

    async function loadBookings() {
        const user = SP.getUser();
        if (!user || !user.name) {
            subtitle.textContent = 'Sign in to see and manage your bookings.';
            wrap.innerHTML = '<div class="empty-state"><h3>You are not signed in</h3>' +
                '<p>Sign in with your name to keep track of your parking bookings.</p>' +
                '<button class="btn btn-primary" type="button" data-open-login>Sign in</button></div>';
            return;
        }
        subtitle.textContent = 'Reserved for ' + user.name;
        try {
            const data = await SP.api('/bookings?user=' + encodeURIComponent(user.name));
            renderBookings(data.bookings || []);
        } catch (error) {
            wrap.innerHTML = '<div class="empty-state"><h3>Could not load bookings</h3><p>' +
                SP.escape(error.message) + '</p></div>';
        }
    }

    async function cancelBooking(id) {
        const user = SP.getUser();
        if (!user) {
            SP.toast('Sign in to cancel a booking', 'error');
            return;
        }
        if (!window.confirm('Cancel booking #' + id + '? The slot will be released immediately.')) return;
        try {
            const data = await SP.api('/bookings/' + id + '/cancel', {
                body: { user: user.name }
            });
            SP.toast(data.message || 'Booking cancelled');
            if (data.promoted && data.promotedBooking) {
                SP.toast('Slot offered to ' + data.promotedBooking.userName + ' from the waiting list');
            }
            if (reference && Number(reference) === id) {
                confirmation.innerHTML = '<div class="empty-state"><h3>Booking cancelled</h3>' +
                    '<p>Booking #' + id + ' was cancelled and the slot is free again.</p>' +
                    '<a class="btn btn-primary" href="/find.html">Find another slot</a></div>';
            }
            loadBookings();
        } catch (error) {
            SP.toast(error.message, 'error');
        }
    }

    document.getElementById('refresh-bookings').addEventListener('click', loadBookings);

    loadConfirmation();
    loadBookings();
})();
