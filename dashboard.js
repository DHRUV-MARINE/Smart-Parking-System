/* ==========================================================================
   Dashboard: renders /api/stats (aggregated in C++).
   ========================================================================== */
(function () {
    'use strict';

    const kpiGrid = document.getElementById('kpi-grid');
    const lotsBody = document.querySelector('#lots-table tbody');
    const busiest = document.getElementById('busiest-list');
    const bookingsWrap = document.getElementById('bookings-table-wrap');

    function kpi(label, value, sub, accent) {
        return '<div class="kpi' + (accent ? ' accent' : '') + '">' +
            '<div class="kpi-label">' + label + '</div>' +
            '<div class="kpi-value">' + value + '</div>' +
            '<div class="kpi-sub">' + sub + '</div></div>';
    }

    function render(data) {
        kpiGrid.innerHTML =
            kpi('Free slots', data.availableSlots + '<span style="font-size:15px;color:var(--muted)"> / ' + data.totalSlots + '</span>',
                'across ' + data.locations + ' parking locations', true) +
            kpi('Occupancy', Math.round(data.occupancyPercent) + '%',
                data.occupiedSlots + ' slots currently occupied') +
            kpi('Active bookings', data.activeBookings,
                'revenue booked ' + SP.money(data.revenue)) +
            kpi('Waitlisted drivers', data.waitingTotal,
                'FIFO queues across full lots');

        document.getElementById('graph-nodes').textContent = data.graphNodes;
        document.getElementById('graph-edges').textContent = data.graphEdges;
        document.getElementById('waiting-total').textContent = data.waitingTotal;
        document.getElementById('revenue').textContent = SP.money(data.revenue);

        busiest.innerHTML = (data.busiest || []).map(function (lot) {
            const pct = Math.round(lot.occupancyPercent);
            return '<div class="bar-item">' +
                '<div class="bar-top"><strong>' + SP.escape(lot.name) + '</strong><span>' + pct + '%</span></div>' +
                '<div class="bar"><span style="width:' + pct + '%"></span></div></div>';
        }).join('');

        lotsBody.innerHTML = (data.lots || []).map(function (lot) {
            return '<tr>' +
                '<td class="name-cell"><a href="/parking.html?id=' + lot.id + '">' + SP.escape(lot.name) + '</a></td>' +
                '<td>' + SP.escape(lot.type || '—') + '</td>' +
                '<td><strong>' + lot.availableSlots + '</strong></td>' +
                '<td>' + lot.occupiedSlots + '</td>' +
                '<td>' + lot.totalSlots + '</td>' +
                '<td>' + SP.meter(lot.availableSlots, lot.totalSlots) +
                '<div class="small muted mt-1">' + Math.round(lot.occupancyPercent) + '%</div></td>' +
                '<td>' + SP.badge(lot.status) + '</td>' +
                '<td>' + lot.waiting + '</td>' +
                '<td>' + SP.money(lot.pricePerHour) + '</td></tr>';
        }).join('');

        document.getElementById('active-count').textContent = data.activeBookings + ' active';
        if (!data.bookings || !data.bookings.length) {
            bookingsWrap.innerHTML = '<div class="empty-state" style="padding:30px"><h3>No active bookings</h3>' +
                '<p>Book a slot from the find parking page and it will show up here.</p>' +
                '<a class="btn btn-secondary btn-sm" href="/find.html">Find parking</a></div>';
        } else {
            bookingsWrap.innerHTML = '<table class="table"><thead><tr>' +
                '<th>Ref</th><th>Parking</th><th>Slot</th><th>User</th><th>Hours</th><th>Amount</th>' +
                '</tr></thead><tbody>' +
                data.bookings.map(function (booking) {
                    return '<tr><td>#' + booking.id + '</td>' +
                        '<td class="name-cell">' + SP.escape(booking.locationName) + '</td>' +
                        '<td>' + SP.escape(booking.slotLabel) + '</td>' +
                        '<td>' + SP.escape(booking.userName) + '</td>' +
                        '<td>' + booking.hours + '</td>' +
                        '<td>' + SP.money(booking.amount) + '</td></tr>';
                }).join('') + '</tbody></table>';
        }
    }

    async function load() {
        document.getElementById('updated-at').textContent = 'Refreshing…';
        try {
            const data = await SP.api('/stats');
            render(data);
            document.getElementById('updated-at').textContent =
                'Updated ' + new Date().toLocaleTimeString() + ' · source: C++ backend';
        } catch (error) {
            document.getElementById('updated-at').textContent = 'Backend not reachable: ' + error.message;
        }
    }

    document.getElementById('refresh-stats').addEventListener('click', load);
    load();
})();
