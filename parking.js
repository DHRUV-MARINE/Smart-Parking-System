/* ==========================================================================
   Parking details + booking.
   The price estimate is requested from the C++ backend (/api/compare) so the
   tiered pricing logic lives in exactly one place.
   ========================================================================== */
(function () {
    'use strict';

    const params = new URLSearchParams(window.location.search);
    const locationId = Number(params.get('id'));
    const savedPlace = SP.getPlace();
    const originPlace = params.get('place') || (savedPlace && savedPlace.name) || 'Mall Road';

    const state = {
        location: null,
        slots: [],
        selectedSlotId: 0,
        hours: 2,
        total: 0
    };

    let estimateSeq = 0;   // ignore stale /api/compare responses

    const elements = {
        badge: document.getElementById('detail-badges'),
        name: document.getElementById('detail-name'),
        address: document.getElementById('detail-address'),
        distance: document.getElementById('detail-distance'),
        crumb: document.getElementById('crumb-name'),
        overviewBadge: document.getElementById('overview-badge'),
        overviewMeter: document.getElementById('overview-meter'),
        overviewFacts: document.getElementById('overview-facts'),
        overviewNote: document.getElementById('overview-note'),
        slotGrid: document.getElementById('slot-grid'),
        slotCount: document.getElementById('slots-count'),
        selectedSlot: document.getElementById('selected-slot'),
        rateLine: document.getElementById('rate-line'),
        estimate: document.getElementById('estimate'),
        hoursInput: document.getElementById('hours-input'),
        hoursHint: document.getElementById('hours-hint'),
        bookBtn: document.getElementById('book-btn'),
        waitlistBtn: document.getElementById('waitlist-btn'),
        bookHint: document.getElementById('book-hint'),
        panelStatus: document.getElementById('panel-status'),
        waitingNote: document.getElementById('waiting-note'),
        priceBody: document.getElementById('price-body'),
        priceNote: document.getElementById('price-note'),
        route: document.getElementById('route-area'),
        compare: document.querySelector('#compare-table tbody')
    };

    function renderOverview() {
        const loc = state.location;
        elements.badge.innerHTML = SP.badge(loc.status) +
            '<span class="tag" style="background:rgba(255,255,255,.1);color:#dbeafe">' + SP.escape(loc.type) + '</span>' +
            '<span class="tag" style="background:rgba(255,255,255,.1);color:#dbeafe">★ ' + Number(loc.rating).toFixed(1) + '</span>';
        elements.name.textContent = loc.name;
        elements.address.textContent = loc.address;
        elements.crumb.textContent = loc.name;
        document.title = loc.name + ' — SmartParking';
        elements.distance.textContent = loc.reachable
            ? SP.km(loc.distanceKm) + ' from ' + originPlace
            : 'Distance unavailable';

        elements.overviewBadge.innerHTML = SP.badge(loc.status);
        elements.overviewMeter.innerHTML = SP.meter(loc.availableSlots, loc.totalSlots);
        elements.overviewFacts.innerHTML = '' +
            '<div class="fact"><span>Free slots</span><strong>' + loc.availableSlots + '</strong></div>' +
            '<div class="fact"><span>Occupied</span><strong>' + (loc.totalSlots - loc.availableSlots) + '</strong></div>' +
            '<div class="fact"><span>Total slots</span><strong>' + loc.totalSlots + '</strong></div>' +
            '<div class="fact"><span>Open hours</span><strong style="font-size:14px">' + SP.escape(loc.openHours) + '</strong></div>';
        elements.overviewNote.textContent = loc.waiting > 0
            ? loc.waiting + ' driver(s) are already queued for this lot.'
            : 'Availability updates the moment a slot is booked or cancelled.';

        elements.priceNote.textContent = loc.type + ' parking';
        elements.priceBody.innerHTML = '' +
            '<tr><td>1 hour</td><td>' + SP.money(loc.pricePerHour) + '</td><td>' + SP.money(loc.pricePerHour) + ' / hr</td></tr>' +
            '<tr><td>3 hours</td><td>' + SP.money(loc.price3Hours) + '</td><td>' + SP.money(loc.price3Hours / 3) + ' / hr</td></tr>' +
            '<tr><td>Full day (24h)</td><td>' + SP.money(loc.pricePerDay) + '</td><td>' + SP.money(loc.pricePerDay / 24) + ' / hr</td></tr>';

        elements.rateLine.textContent = SP.money(loc.pricePerHour) + ' / hr';
        elements.waitingNote.textContent = loc.waiting > 0
            ? loc.waiting + ' person(s) waiting · promoted first-in-first-out when a slot frees up'
            : 'Nobody is waiting for this lot.';
    }

    function renderSlots() {
        const total = state.slots.length;
        const free = state.slots.filter(function (slot) { return !slot.occupied; }).length;
        elements.slotCount.textContent = free + ' free of ' + total;

        elements.slotGrid.innerHTML = state.slots.map(function (slot) {
            const selected = slot.id === state.selectedSlotId;
            const classes = ['slot'];
            if (slot.occupied) classes.push('is-occupied');
            else classes.push('is-free');
            if (selected) classes.push('is-selected');
            return '<button type="button" class="' + classes.join(' ') + '" data-slot="' + slot.id + '"' +
                (slot.occupied ? ' disabled' : '') + ' aria-pressed="' + selected + '">' + SP.escape(slot.label) + '</button>';
        }).join('');

        elements.slotGrid.querySelectorAll('button[data-slot]').forEach(function (button) {
            button.addEventListener('click', function () {
                const id = Number(button.getAttribute('data-slot'));
                state.selectedSlotId = state.selectedSlotId === id ? 0 : id;
                renderSlots();
                const chosen = state.slots.find(function (slot) { return slot.id === state.selectedSlotId; });
                elements.selectedSlot.textContent = chosen ? chosen.label : 'Auto-assign';
            });
        });
    }

    async function updateEstimate() {
        state.hours = Math.min(168, Math.max(1, Number(elements.hoursInput.value) || 1));
        elements.hoursInput.value = state.hours;
        if (state.location && state.location.availableSlots <= 0) {
            elements.estimate.textContent = '—';
            elements.hoursHint.textContent = 'No slots available at the moment.';
            return;
        }
        const seq = ++estimateSeq;
        try {
            const data = await SP.api('/compare?ids=' + locationId + '&hours=' + state.hours);
            if (seq !== estimateSeq) return;
            const row = (data.results || [])[0];
            if (row) {
                state.total = row.amount;
                elements.estimate.textContent = SP.money(row.amount);
                elements.hoursHint.textContent = state.hours + ' hour' + (state.hours === 1 ? '' : 's') +
                    ' · ' + SP.money(row.pricePerHour) + '/hr base rate, tiered pricing applied';
            }
        } catch (error) {
            elements.estimate.textContent = '—';
        }
    }

    function renderRoute(route) {
        if (!route || !route.reachable) {
            elements.route.innerHTML = '<p class="muted small">No origin selected, or this lot is not connected to the road graph.</p>';
            return;
        }
        const steps = (route.path || []).map(function (step) {
            return '<span class="tag">' + SP.escape(step) + '</span>';
        }).join('<span class="arrow">→</span>');
        elements.route.innerHTML =
            '<div class="row-between mb-2"><strong>' + SP.km(route.distanceKm) + '</strong>' +
            '<span class="tag green">shortest path</span></div>' +
            '<div class="route-steps">' + steps + '</div>' +
            '<p class="hint mt-2">Computed by Dijkstra\'s algorithm in C++ (' + (route.algorithm || 'binary min-heap') + ').</p>';
    }

    async function loadCompare() {
        try {
            const data = await SP.api('/locations' + SP.buildQuery({
                place: originPlace, sort: 'price', includeFull: 1, limit: 5
            }));
            const rows = (data.results || []).slice(0, 5);
            elements.compare.innerHTML = rows.map(function (lot) {
                const current = lot.id === locationId;
                return '<tr' + (current ? ' style="background:#f0fdf4"' : '') + '>' +
                    '<td class="name-cell">' + SP.escape(lot.name) + (current ? ' <span class="tag green">this lot</span>' : '') + '</td>' +
                    '<td>' + SP.money(lot.pricePerHour) + '</td>' +
                    '<td>' + SP.money(lot.price3Hours) + '</td>' +
                    '<td>' + SP.money(lot.pricePerDay) + '</td>' +
                    '<td>' + lot.availableSlots + '</td>' +
                    '<td><a class="btn btn-secondary btn-sm" href="/parking.html?id=' + lot.id +
                    '&place=' + encodeURIComponent(originPlace) + '">View</a></td></tr>';
            }).join('');
        } catch (error) {
            elements.compare.innerHTML = '<tr><td colspan="6" class="muted">Comparison unavailable</td></tr>';
        }
    }

    function configureBookingPanel() {
        const loc = state.location;
        const isFull = loc.availableSlots <= 0;
        elements.bookBtn.disabled = isFull;
        elements.bookBtn.classList.toggle('hidden', isFull);
        elements.waitlistBtn.classList.toggle('hidden', !isFull);
        elements.panelStatus.innerHTML = SP.badge(loc.status);
        elements.bookHint.textContent = isFull
            ? 'This lot is full right now. Join the waiting list and the first free slot is offered to you automatically.'
            : 'Booking updates availability immediately. You can cancel any time from “My bookings”.';
        if (isFull) {
            elements.estimate.textContent = '—';
            elements.hoursHint.textContent = 'No slots available at the moment.';
        }
    }

    async function load() {
        if (!locationId || Number.isNaN(locationId)) {
            elements.name.textContent = 'Parking not found';
            document.querySelector('main').innerHTML =
                '<div class="empty-state" style="margin:40px 0"><h3>That parking id is not valid</h3>' +
                '<p>Pick a location from the find parking page.</p><a class="btn btn-primary" href="/find.html">Find parking</a></div>';
            return;
        }
        try {
            const query = SP.buildQuery({ place: originPlace });
            const data = await SP.api('/locations/' + locationId + query);
            state.location = data.location;
            state.slots = data.slots || [];
            renderOverview();
            renderSlots();
            configureBookingPanel();
            await updateEstimate();

            if (state.location.reachable) {
                try {
                    const route = await SP.api('/locations/' + locationId + '/route' + query);
                    renderRoute(route);
                } catch (error) {
                    renderRoute(null);
                }
            } else {
                renderRoute(null);
            }
            loadCompare();
        } catch (error) {
            elements.name.textContent = 'Could not load this parking location';
            elements.address.textContent = error.message;
        }
    }

    /* ---------------- booking ---------------- */
    elements.bookBtn.addEventListener('click', function () {
        if (!state.location) return;
        if (state.location.availableSlots <= 0) {
            SP.toast('This lot just became full - try the waiting list', 'error');
            load();
            return;
        }
        SP.requireUser(async function (user) {
            elements.bookBtn.disabled = true;
            elements.bookBtn.textContent = 'Booking…';
            try {
                const data = await SP.api('/bookings', {
                    body: {
                        locationId: locationId,
                        slotId: state.selectedSlotId,
                        hours: state.hours,
                        user: user.name
                    }
                });
                SP.toast(data.message || 'Slot booked');
                window.location.href = '/booking.html?ref=' + data.booking.id;
            } catch (error) {
                SP.toast(error.message, 'error');
                elements.bookBtn.disabled = false;
                elements.bookBtn.textContent = 'Book this slot';
                load();
            }
        });
    });

    elements.waitlistBtn.addEventListener('click', function () {
        SP.requireUser(async function (user) {
            elements.waitlistBtn.disabled = true;
            try {
                const data = await SP.api('/waitlist', {
                    body: { locationId: locationId, user: user.name }
                });
                SP.toast(data.message || 'Added to the waiting list');
                elements.waitingNote.textContent = 'You are on the waiting list. Position: ' + data.position;
                elements.waitlistBtn.textContent = 'You are on the waiting list';
            } catch (error) {
                SP.toast(error.message, 'error');
                elements.waitlistBtn.disabled = false;
            }
        });
    });

    document.getElementById('hours-minus').addEventListener('click', function () {
        elements.hoursInput.value = Math.max(1, state.hours - 1);
        updateEstimate();
    });
    document.getElementById('hours-plus').addEventListener('click', function () {
        elements.hoursInput.value = Math.min(168, state.hours + 1);
        updateEstimate();
    });
    elements.hoursInput.addEventListener('change', updateEstimate);

    load();
})();
