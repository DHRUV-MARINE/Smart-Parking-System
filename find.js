/* ==========================================================================
   Find parking page. All filtering / sorting happens in the C++ backend:
   this script only collects the filter values and paints the response.
   ========================================================================== */
(function () {
    'use strict';

    const params = new URLSearchParams(window.location.search);
    const saved = SP.getPlace();

    const state = {
        place: params.get('place') || (saved && saved.name) || 'Mall Road',
        q: params.get('q') || '',
        sort: params.get('sort') || 'distance',
        maxPrice: 60,
        maxDistance: 20,
        minAvailable: 0,
        type: '',
        lat: null,
        lng: null,
        selectedId: null
    };

    let lastResults = [];
    let requestSeq = 0;   // guards against an older response overwriting a newer one
    let originCoords = null;
    let originName = state.place;

    const placeInput = document.getElementById('place-input');
    const queryInput = document.getElementById('query-input');
    const sortSelect = document.getElementById('sort-select');
    const resultsList = document.getElementById('results-list');
    const resultsMeta = document.getElementById('results-meta');
    const mapNote = document.getElementById('map-note');

    placeInput.value = state.place;
    queryInput.value = state.q;
    sortSelect.value = state.sort;

    function sortLabel(sort) {
        if (sort === 'price') return 'cheapest first · quick sort';
        if (sort === 'availability') return 'most free slots · merge sort';
        if (sort === 'name') return 'name A–Z · merge sort';
        return 'nearest first · Dijkstra';
    }

    function paramsFromState() {
        return SP.buildQuery({
            place: state.place,
            q: state.q,
            sort: state.sort,
            maxPrice: state.maxPrice < 60 ? state.maxPrice : '',
            maxDistance: state.maxDistance < 20 ? state.maxDistance : '',
            minAvailable: state.minAvailable > 0 ? state.minAvailable : '',
            type: state.type,
            lat: state.place ? '' : state.lat,
            lng: state.place ? '' : state.lng,
            includeFull: 1
        });
    }

    function row(lot) {
        const selected = lot.id === state.selectedId ? ' selected' : '';
        const distance = lot.reachable ? SP.km(lot.distanceKm) : '—';
        const action = lot.availableSlots > 0
            ? '<a class="btn btn-primary btn-sm" href="/parking.html?id=' + lot.id + '">Book slot</a>'
            : '<a class="btn btn-secondary btn-sm" href="/parking.html?id=' + lot.id + '">Join waiting list</a>';
        return '' +
            '<article class="lot-row' + selected + '" data-id="' + lot.id + '">' +
            '<div>' +
            '<div class="row wrap" style="gap:8px">' + SP.badge(lot.status) +
            '<span class="tag">' + SP.escape(lot.type) + '</span>' +
            '<span class="tag green">' + (lot.reachable ? distance + ' away' : 'distance unknown') + '</span>' +
            (lot.waiting > 0 ? '<span class="tag">' + lot.waiting + ' waiting</span>' : '') +
            '</div>' +
            '<h3>' + SP.escape(lot.name) + '</h3>' +
            '<div class="row-sub">' +
            '<span>' + SP.escape(lot.address) + '</span>' +
            '</div>' +
            '<div class="tags">' +
            '<span class="tag">' + lot.availableSlots + ' of ' + lot.totalSlots + ' free</span>' +
            '<span class="tag">★ ' + Number(lot.rating).toFixed(1) + '</span>' +
            '<span class="tag">' + SP.escape(lot.openHours) + '</span>' +
            '</div>' +
            '<div class="mt-2">' + SP.meter(lot.availableSlots, lot.totalSlots) + '</div>' +
            '</div>' +
            '<div class="row-side">' +
            '<div class="price">' + SP.money(lot.pricePerHour) + ' <span>/ hour</span>' +
            '<div class="small muted" style="font-weight:400">' + SP.money(lot.price3Hours) + ' · 3 hours</div>' +
            '<div class="small muted" style="font-weight:400">' + SP.money(lot.pricePerDay) + ' · full day</div>' +
            '</div>' +
            '<div class="row" style="gap:8px">' +
            '<a class="btn btn-secondary btn-sm" href="/parking.html?id=' + lot.id + '">Details</a>' +
            action +
            '</div></div></article>';
    }

    function render() {
        if (!lastResults.length) {
            resultsList.innerHTML = '<div class="empty-state"><h3>No parking matches these filters</h3>' +
                '<p>Raise the price or distance limit, or clear the search text.</p>' +
                '<button class="btn btn-secondary" type="button" id="empty-reset">Reset filters</button></div>';
            const reset = document.getElementById('empty-reset');
            if (reset) reset.addEventListener('click', resetFilters);
            return;
        }

        resultsList.innerHTML = lastResults.map(row).join('');
        resultsList.querySelectorAll('.lot-row').forEach(function (element) {
            element.addEventListener('click', function (event) {
                if (event.target.closest('a')) return;
                state.selectedId = Number(element.getAttribute('data-id'));
                renderMap();
                resultsList.querySelectorAll('.lot-row').forEach(function (node) {
                    node.classList.toggle('selected', Number(node.getAttribute('data-id')) === state.selectedId);
                });
            });
        });
    }

    function renderMap() {
        SP.renderMap(document.getElementById('find-map'), {
            lots: lastResults,
            origin: originCoords,
            selectedId: state.selectedId,
            height: 380,
            onSelect: function (id) {
                state.selectedId = id;
                renderMap();
                const target = resultsList.querySelector('.lot-row[data-id="' + id + '"]');
                if (target) {
                    target.classList.add('selected');
                    target.scrollIntoView({ behavior: 'smooth', block: 'center' });
                }
            }
        });
        mapNote.innerHTML = 'Simulated map · ' + (originCoords ? SP.escape(originName) : 'no origin selected') +
            ' · marker colours: green available, amber limited, red full';
    }

    async function load() {
        const seq = ++requestSeq;
        resultsMeta.textContent = 'Searching…';
        try {
            const data = await SP.api('/locations' + paramsFromState());
            if (seq !== requestSeq) return;   // a newer search is already in flight
            lastResults = data.results || [];
            originName = data.originName || state.place;
            if (data.originName) {
                originCoords = SP.originCoords[data.originName] || (originCoords || null);
                if (!originCoords) {
                    const first = lastResults.find(function (lot) { return lot.distanceKm === 0; });
                    if (first) originCoords = { name: data.originName, lat: first.lat, lng: first.lng };
                }
            }
            if (!state.selectedId && lastResults.length) state.selectedId = lastResults[0].id;

            resultsMeta.innerHTML = '<strong>' + lastResults.length + '</strong> lots · ' +
                sortLabel(state.sort) + (originName ? ' · from ' + SP.escape(originName) : '');
            render();
            renderMap();
        } catch (error) {
            if (seq !== requestSeq) return;
            resultsMeta.textContent = 'Could not load';
            resultsList.innerHTML = '<div class="empty-state"><h3>Backend not reachable</h3><p>' +
                SP.escape(error.message) + '</p><p class="small">Make sure the C++ server is running.</p></div>';
        }
    }

    function refreshLabels() {
        document.getElementById('price-value').textContent =
            state.maxPrice < 60 ? 'Up to ' + SP.money(state.maxPrice) + '/hr' : 'Any price';
        document.getElementById('distance-value').textContent =
            state.maxDistance < 20 ? 'Within ' + state.maxDistance + ' km' : 'Any distance';
        document.getElementById('available-value').textContent =
            state.minAvailable > 0 ? 'At least ' + state.minAvailable : 'Any';
    }

    function resetFilters() {
        state.maxPrice = 60;
        state.maxDistance = 20;
        state.minAvailable = 0;
        state.type = '';
        state.q = '';
        queryInput.value = '';
        document.getElementById('max-price').value = 60;
        document.getElementById('max-distance').value = 20;
        document.getElementById('min-available').value = 0;
        document.querySelectorAll('#type-filter button').forEach(function (button) {
            button.classList.toggle('active', button.getAttribute('data-type') === '');
        });
        refreshLabels();
        load();
    }

    /* ---------------- events ---------------- */
    document.getElementById('search-form').addEventListener('submit', function (event) {
        event.preventDefault();
        state.place = placeInput.value.trim();
        state.q = queryInput.value.trim();
        state.sort = sortSelect.value;
        state.lat = null;
        state.lng = null;
        state.selectedId = null;
        originCoords = null;
        if (state.place) {
            const coords = SP.originCoords[state.place];
            SP.setPlace(state.place, coords && coords.lat, coords && coords.lng);
        }
        load();
    });

    document.getElementById('max-price').addEventListener('input', function (event) {
        state.maxPrice = Number(event.target.value);
        refreshLabels();
        load();
    });
    document.getElementById('max-distance').addEventListener('input', function (event) {
        state.maxDistance = Number(event.target.value);
        refreshLabels();
        load();
    });
    document.getElementById('min-available').addEventListener('input', function (event) {
        state.minAvailable = Number(event.target.value);
        refreshLabels();
        load();
    });

    document.querySelectorAll('#type-filter button').forEach(function (button) {
        button.addEventListener('click', function () {
            state.type = button.getAttribute('data-type');
            document.querySelectorAll('#type-filter button').forEach(function (other) {
                other.classList.toggle('active', other === button);
            });
            load();
        });
    });

    document.getElementById('reset-filters').addEventListener('click', resetFilters);

    document.getElementById('geo-btn').addEventListener('click', function () {
        if (!navigator.geolocation) {
            SP.toast('This browser does not support geolocation', 'error');
            return;
        }
        const button = document.getElementById('geo-btn');
        button.disabled = true;
        navigator.geolocation.getCurrentPosition(function (position) {
            button.disabled = false;
            state.lat = position.coords.latitude;
            state.lng = position.coords.longitude;
            state.place = '';
            placeInput.value = 'My current location';
            state.selectedId = null;
            originCoords = { name: 'My location', lat: state.lat, lng: state.lng };
            SP.toast('Using your current location');
            load();
        }, function () {
            button.disabled = false;
            SP.toast('Location permission denied - pick a landmark instead', 'error');
        }, { timeout: 8000 });
    });

    (async function init() {
        try {
            const origins = await SP.origins();
            document.getElementById('place-suggestions').innerHTML = origins.map(function (origin) {
                return '<option value="' + SP.escape(origin.name) + '"></option>';
            }).join('');
        } catch (error) { /* optional */ }
        refreshLabels();
        load();
    })();
})();
