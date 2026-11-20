/* ==========================================================================
   Landing page logic: live stats, nearest lots and the hero mini-map.
   ========================================================================== */
(function () {
    'use strict';

    const nearbyGrid = document.getElementById('nearby-grid');
    const stats = {
        free: document.getElementById('stat-free'),
        locations: document.getElementById('stat-locations'),
        total: document.getElementById('stat-total')
    };

    let currentPlace = (SP.getPlace() && SP.getPlace().name) || 'Mall Road';

    function lotCard(lot) {
        const href = '/parking.html?id=' + lot.id;
        const distance = lot.reachable ? SP.km(lot.distanceKm) : '—';
        const bookLabel = lot.availableSlots > 0 ? 'Book slot' : 'Waiting list';
        return '' +
            '<article class="lot-card">' +
            '<div class="lot-top"><div>' +
            '<span class="lot-area">' + SP.escape(lot.area) + '</span>' +
            '<h3>' + SP.escape(lot.name) + '</h3>' +
            '</div>' + SP.badge(lot.status) + '</div>' +
            '<p class="lot-address">' +
            '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 21s7-6.3 7-11a7 7 0 10-14 0c0 4.7 7 11 7 11z"></path><circle cx="12" cy="10" r="2.5"></circle></svg>' +
            SP.escape(lot.address) + '</p>' +
            '<div class="lot-stats">' +
            '<div><strong>' + lot.availableSlots + '</strong><span>free now</span></div>' +
            '<div><strong>' + lot.totalSlots + '</strong><span>total</span></div>' +
            '<div><strong>' + SP.money(lot.pricePerHour) + '</strong><span>per hour</span></div>' +
            '<div><strong>' + distance + '</strong><span>away</span></div>' +
            '</div>' +
            SP.meter(lot.availableSlots, lot.totalSlots) +
            '<div class="lot-meta">' +
            '<span>' + SP.escape(lot.type) + '</span><span class="dot-sep"></span>' +
            '<span>★ ' + Number(lot.rating).toFixed(1) + '</span><span class="dot-sep"></span>' +
            '<span>' + SP.escape(lot.openHours) + '</span>' +
            '</div>' +
            '<div class="lot-actions">' +
            '<a class="btn btn-secondary btn-sm" href="' + href + '">Details</a>' +
            '<a class="btn btn-primary btn-sm" href="' + href + '#book">' + bookLabel + '</a>' +
            '</div></article>';
    }

    async function loadNearby() {
        try {
            const query = SP.buildQuery({ place: currentPlace, sort: 'distance', limit: 6 });
            const data = await SP.api('/locations' + query);
            const lots = data.results || [];
            if (!lots.length) {
                nearbyGrid.innerHTML = '<div class="empty-state" style="grid-column:1/-1"><h3>No parking found</h3><p>Try another area.</p></div>';
                return;
            }
            nearbyGrid.innerHTML = lots.map(lotCard).join('');

            const place = lots[0];
            if (data.originName) {
                currentPlace = data.originName;
                document.getElementById('hero-map-origin').textContent = data.originName;
            }
            const nearest = document.getElementById('nearest-name');
            const meta = document.getElementById('nearest-meta');
            if (nearest && place) {
                nearest.textContent = place.name;
                meta.textContent = SP.km(place.distanceKm) + ' away · ' + place.availableSlots + ' slots free';
            }

            const origin = findOriginCoords(data.originName, lots);
            SP.renderMap(document.getElementById('hero-map'), {
                lots: lots,
                origin: origin,
                selectedId: place ? place.id : undefined,
                height: 400,
                onSelect: function (id) { window.location.href = '/parking.html?id=' + id; }
            });
            document.getElementById('hero-legend').innerHTML = SP.mapLegend();
        } catch (error) {
            nearbyGrid.innerHTML = '<div class="empty-state" style="grid-column:1/-1"><h3>Backend not reachable</h3><p>' +
                SP.escape(error.message) + '</p></div>';
        }
    }

    function findOriginCoords(name, lots) {
        if (!name) return null;
        const coords = SP.originCoords[name];
        if (coords) return { name: name, lat: coords.lat, lng: coords.lng };
        // fall back: the nearest lot, or the saved place
        const saved = SP.getPlace();
        if (saved && saved.lat) return saved;
        if (lots.length) return { name: lots[0].name, lat: lots[0].lat, lng: lots[0].lng };
        return null;
    }

    async function loadStats() {
        try {
            const data = await SP.api('/stats');
            stats.free.textContent = data.availableSlots;
            stats.locations.textContent = data.locations;
            stats.total.textContent = data.totalSlots;
        } catch (error) {
            stats.free.textContent = '—';
            stats.locations.textContent = '—';
            stats.total.textContent = '—';
        }
    }

    async function loadSuggestions() {
        try {
            const origins = await SP.origins();
            const list = document.getElementById('place-suggestions');
            list.innerHTML = origins.map(function (origin) {
                return '<option value="' + SP.escape(origin.name) + '"></option>';
            }).join('');
        } catch (error) { /* suggestions are optional */ }
    }

    document.getElementById('hero-search').addEventListener('submit', function (event) {
        event.preventDefault();
        const value = document.getElementById('hero-place').value.trim();
        const params = {};
        if (!value) {
            params.place = currentPlace;
        } else {
            params.place = value;
            params.q = value;
        }
        window.location.href = '/find.html' + SP.buildQuery(params);
    });

    document.querySelectorAll('#quick-places [data-place]').forEach(function (chip) {
        chip.addEventListener('click', function () {
            SP.setPlace(chip.getAttribute('data-place'));
            window.location.href = '/find.html' + SP.buildQuery({ place: chip.getAttribute('data-place') });
        });
    });

    loadStats();
    loadSuggestions();
    loadNearby();
})();
