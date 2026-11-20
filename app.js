/* ==========================================================================
   SmartParking - shared frontend helpers.
   The frontend only renders what the C++ backend computes: distances
   (Dijkstra), availability, sorting, filtering and pricing all come from the
   REST API.
   ========================================================================== */
(function () {
    'use strict';

    const USER_KEY = 'smartparking.user';
    const PLACE_KEY = 'smartparking.place';

    const SP = (window.SP = {});
    let originCache = null;

    /* ---------------------------------------------------------------------
       Formatting helpers
       --------------------------------------------------------------------- */
    SP.escape = function (value) {
        return String(value == null ? '' : value)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;')
            .replace(/'/g, '&#039;');
    };

    SP.money = function (amount) {
        const value = Number(amount) || 0;
        return '₹' + value.toLocaleString('en-IN', { maximumFractionDigits: 0 });
    };

    SP.km = function (distance) {
        const value = Number(distance) || 0;
        if (value < 1) return Math.round(value * 1000) + ' m';
        return value.toFixed(1) + ' km';
    };

    SP.statusKey = function (status) {
        if (status === 'Full') return 'full';
        if (status === 'Limited') return 'limited';
        return 'available';
    };

    SP.badge = function (status) {
        const key = SP.statusKey(status);
        return '<span class="badge badge-' + key + '"><i></i>' + SP.escape(status) + '</span>';
    };

    SP.meter = function (available, total) {
        const pct = total > 0 ? Math.round((available / total) * 100) : 0;
        const status = total === 0 ? 'full' : available === 0 ? 'full' : available <= Math.max(3, total / 5) ? 'limited' : 'available';
        return '<div class="meter ' + (status === 'full' ? 'is-full' : status === 'limited' ? 'is-limited' : '') +
            '"><span style="width:' + Math.max(pct, available > 0 ? 4 : 0) + '%"></span></div>';
    };

    SP.initials = function (name) {
        return String(name || '?')
            .trim()
            .split(/\s+/)
            .slice(0, 2)
            .map(function (part) { return part.charAt(0).toUpperCase(); })
            .join('');
    };

    // Shared coordinates for the simulated map's origin marker.
    SP.originCoords = {
        'The Ridge': { lat: 31.1041, lng: 77.1737 },
        'Mall Road': { lat: 31.1043, lng: 77.1745 },
        'ISBT Tutikandi': { lat: 31.0903, lng: 77.1447 },
        'Sanjauli Chowk': { lat: 31.1006, lng: 77.1930 },
        'Victory Tunnel': { lat: 31.1043, lng: 77.1655 },
        'Chhota Shimla': { lat: 31.0900, lng: 77.1620 },
        'New Shimla': { lat: 31.0794, lng: 77.1620 },
        'Summer Hill': { lat: 31.1023, lng: 77.1390 },
        'Kufri': { lat: 31.0979, lng: 77.2680 },
        'Jakhoo Temple': { lat: 31.1003, lng: 77.1855 },
        'Shimla Railway Station': { lat: 31.1036, lng: 77.1590 },
        'Dhalli Junction': { lat: 31.0870, lng: 77.1200 }
    };

    /* ---------------------------------------------------------------------
       API wrapper
       --------------------------------------------------------------------- */
    SP.api = async function (path, options) {
        const settings = Object.assign({}, options || {});
        const config = { headers: {} };
        config.method = settings.method || (settings.body ? 'POST' : 'GET');
        if (settings.body) {
            config.headers['Content-Type'] = 'application/json';
            config.body = JSON.stringify(settings.body);
        }
        const response = await fetch('/api' + path, config);
        let payload = null;
        try {
            payload = await response.json();
        } catch (error) {
            payload = null;
        }
        if (!response.ok) {
            const err = new Error((payload && payload.error) || 'Request failed (' + response.status + ')');
            err.status = response.status;
            throw err;
        }
        return payload;
    };

    SP.origins = async function () {
        if (!originCache) {
            const data = await SP.api('/origins');
            originCache = data.origins || [];
        }
        return originCache;
    };

    SP.buildQuery = function (params) {
        const search = new URLSearchParams();
        Object.keys(params).forEach(function (key) {
            const value = params[key];
            if (value === undefined || value === null || value === '') return;
            search.set(key, value);
        });
        const text = search.toString();
        return text ? '?' + text : '';
    };

    /* ---------------------------------------------------------------------
       Saved location + user (browser storage only)
       --------------------------------------------------------------------- */
    SP.setPlace = function (name, lat, lng) {
        const place = { name: name || '', lat: lat, lng: lng };
        try {
            localStorage.setItem(PLACE_KEY, JSON.stringify(place));
        } catch (error) {
            /* private mode - ignore */
        }
        return place;
    };

    SP.getPlace = function () {
        try {
            const raw = localStorage.getItem(PLACE_KEY);
            if (!raw) return null;
            const parsed = JSON.parse(raw);
            if (parsed && parsed.name) return parsed;
        } catch (error) { /* ignore */ }
        return null;
    };

    SP.getUser = function () {
        try {
            const raw = localStorage.getItem(USER_KEY);
            return raw ? JSON.parse(raw) : null;
        } catch (error) {
            return null;
        }
    };

    SP.setUser = function (user) {
        try {
            if (user) localStorage.setItem(USER_KEY, JSON.stringify(user));
            else localStorage.removeItem(USER_KEY);
        } catch (error) { /* ignore */ }
        updateHeader();
    };

    /* ---------------------------------------------------------------------
       Header + navigation
       --------------------------------------------------------------------- */
    function updateHeader() {
        const host = document.querySelector('[data-auth-area]');
        if (!host) return;
        const user = SP.getUser();
        if (user && user.name) {
            host.innerHTML =
                '<a class="user-chip" href="/booking.html" title="My bookings">' +
                '<span class="avatar">' + SP.escape(SP.initials(user.name)) + '</span>' +
                '<span>' + SP.escape(user.name) + '</span></a>' +
                '<button class="link-quiet" type="button" data-logout>Sign out</button>';
        } else {
            host.innerHTML = '<button class="btn btn-primary btn-sm" type="button" data-open-login>Sign in</button>';
        }
    }

    let pendingLogin = null;

    SP.requireUser = function (action) {
        const user = SP.getUser();
        if (user && user.name) {
            action(user);
            return;
        }
        pendingLogin = action;
        openLogin();
    };

    function ensureLoginModal() {
        if (document.getElementById('login-modal')) return;
        const wrapper = document.createElement('div');
        wrapper.className = 'modal-backdrop';
        wrapper.id = 'login-modal';
        wrapper.innerHTML =
            '<div class="modal" role="dialog" aria-modal="true" aria-labelledby="login-title">' +
            '<h2 id="login-title">Sign in to SmartParking</h2>' +
            '<p class="sub">Demo sign-in for the academic project: your name is saved in this browser only. No password is stored.</p>' +
            '<form id="login-form" novalidate>' +
            '<div class="field"><label for="login-name">Your name</label>' +
            '<input class="input" id="login-name" name="name" maxlength="40" placeholder="e.g. Asha Rao" required></div>' +
            '<div class="field"><label for="login-email">Email (optional)</label>' +
            '<input class="input" id="login-email" name="email" type="email" placeholder="asha@example.com"></div>' +
            '<p class="form-error" id="login-error"></p>' +
            '<div class="actions">' +
            '<button type="button" class="btn btn-secondary" data-close-login>Cancel</button>' +
            '<button class="btn btn-primary" type="submit">Continue</button>' +
            '</div></form></div>';
        document.body.appendChild(wrapper);

        wrapper.addEventListener('click', function (event) {
            if (event.target === wrapper || event.target.hasAttribute('data-close-login')) closeLogin();
        });
        wrapper.querySelector('#login-form').addEventListener('submit', function (event) {
            event.preventDefault();
            const name = wrapper.querySelector('#login-name').value.trim();
            const email = wrapper.querySelector('#login-email').value.trim();
            const errorBox = wrapper.querySelector('#login-error');
            if (name.length < 2) {
                errorBox.textContent = 'Please enter your name (at least 2 characters).';
                errorBox.classList.add('show');
                return;
            }
            errorBox.classList.remove('show');
            SP.setUser({ name: name, email: email });
            closeLogin();
            SP.toast('Signed in as ' + name);
            if (pendingLogin) {
                const action = pendingLogin;
                pendingLogin = null;
                action(SP.getUser());
            }
        });
    }

    function openLogin() {
        ensureLoginModal();
        const modal = document.getElementById('login-modal');
        const nameInput = modal.querySelector('#login-name');
        const current = SP.getUser();
        if (current && current.name) nameInput.value = current.name;
        modal.classList.add('open');
        setTimeout(function () { nameInput.focus(); }, 30);
    }

    function closeLogin() {
        const modal = document.getElementById('login-modal');
        if (modal) modal.classList.remove('open');
    }

    /* ---------------------------------------------------------------------
       Toast
       --------------------------------------------------------------------- */
    SP.toast = function (message, type) {
        let host = document.querySelector('.toast-host');
        if (!host) {
            host = document.createElement('div');
            host.className = 'toast-host';
            document.body.appendChild(host);
        }
        const toast = document.createElement('div');
        toast.className = 'toast' + (type === 'error' ? ' error' : '');
        toast.textContent = message;
        host.appendChild(toast);
        setTimeout(function () {
            toast.style.opacity = '0';
            toast.style.transform = 'translateY(6px)';
            toast.style.transition = 'all .2s ease';
            setTimeout(function () { toast.remove(); }, 220);
        }, 4200);
    };

    /* ---------------------------------------------------------------------
       Simulated city map (pure SVG, no map API needed)
       --------------------------------------------------------------------- */
    SP.renderMap = function (mount, options) {
        if (!mount) return;
        const opts = options || {};
        const lots = opts.lots || [];
        const origin = opts.origin || null;
        const selectedId = opts.selectedId;

        if (!lots.length) {
            mount.innerHTML = '<div style="padding:40px;text-align:center;color:#93a5b8;font-size:13px">' +
                'No parking locations match your filters.</div>';
            return;
        }

        const width = 760;
        const height = opts.height || 500;
        const pad = 54;

        const points = lots.map(function (lot) { return { lat: lot.lat, lng: lot.lng }; });
        if (origin && origin.lat && origin.lng) points.push({ lat: origin.lat, lng: origin.lng });

        let minLat = Math.min.apply(null, points.map(function (p) { return p.lat; }));
        let maxLat = Math.max.apply(null, points.map(function (p) { return p.lat; }));
        let minLng = Math.min.apply(null, points.map(function (p) { return p.lng; }));
        let maxLng = Math.max.apply(null, points.map(function (p) { return p.lng; }));

        if (maxLat - minLat < 0.01) { minLat -= 0.01; maxLat += 0.01; }
        if (maxLng - minLng < 0.01) { minLng -= 0.01; maxLng += 0.01; }

        function project(lat, lng) {
            const x = pad + ((lng - minLng) / (maxLng - minLng)) * (width - pad * 2);
            const y = pad + ((maxLat - lat) / (maxLat - minLat)) * (height - pad * 2);
            return { x: x, y: y };
        }

        let svg = '<svg viewBox="0 0 ' + width + ' ' + height + '" role="img" aria-label="Simulated map of nearby parking">';
        svg += '<defs><linearGradient id="mapBg" x1="0" y1="0" x2="0" y2="1">' +
            '<stop offset="0" stop-color="#0c2740"/><stop offset="1" stop-color="#081d30"/></linearGradient>' +
            '<filter id="glow"><feGaussianBlur stdDeviation="6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter></defs>';
        svg += '<rect width="' + width + '" height="' + height + '" fill="url(#mapBg)"/>';

        // dotted grid
        for (let x = 20; x < width; x += 24) {
            for (let y = 20; y < height; y += 24) {
                svg += '<circle cx="' + x + '" cy="' + y + '" r="0.9" fill="rgba(255,255,255,0.07)"/>';
            }
        }

        // decorative city blocks (deterministic)
        const blocks = [
            [70, 70, 120, 78], [230, 56, 96, 62], [360, 96, 130, 70], [540, 62, 120, 84],
            [96, 250, 108, 96], [246, 300, 140, 74], [430, 236, 112, 90], [590, 300, 104, 82],
            [150, 420, 150, 60], [600, 160, 96, 60]
        ];
        blocks.forEach(function (b, index) {
            svg += '<rect x="' + b[0] + '" y="' + b[1] + '" width="' + b[2] + '" height="' + b[3] + '" rx="8" ' +
                'fill="rgba(255,255,255,0.028)" stroke="rgba(255,255,255,0.05)" stroke-width="1"/>';
            if (index % 3 === 0) {
                svg += '<rect x="' + (b[0] + 14) + '" y="' + (b[1] + 12) + '" width="' + (b[2] - 28) + '" height="' + (b[3] / 2) + '" rx="4" fill="rgba(34,197,94,0.05)"/>';
            }
        });

        // roads
        const roads = [
            'M0,210 C160,196 320,232 760,206',
            'M0,352 C220,336 420,384 760,344',
            'M214,0 C204,170 250,330 232,500',
            'M520,0 C504,150 556,320 540,500',
            'M0,120 C240,104 500,150 760,112'
        ];
        roads.forEach(function (d) {
            svg += '<path d="' + d + '" fill="none" stroke="rgba(148,197,255,0.16)" stroke-width="9" stroke-linecap="round"/>';
            svg += '<path d="' + d + '" fill="none" stroke="rgba(148,197,255,0.30)" stroke-width="1" stroke-dasharray="6 10"/>';
        });

        // line from user to a focused lot
        if (origin && origin.lat) {
            const from = project(origin.lat, origin.lng);
            const focus = lots.find(function (lot) { return lot.id === selectedId; }) || lots[0];
            if (focus) {
                const to = project(focus.lat, focus.lng);
                const midX = (from.x + to.x) / 2;
                const midY = (from.y + to.y) / 2 - 26;
                svg += '<path d="M' + from.x + ',' + from.y + ' Q' + midX + ',' + midY + ' ' + to.x + ',' + to.y +
                    '" fill="none" stroke="#22c55e" stroke-width="2" stroke-dasharray="5 7" opacity="0.85"/>';
            }
        }

        // markers
        lots.forEach(function (lot) {
            const point = project(lot.lat, lot.lng);
            const key = SP.statusKey(lot.status);
            const color = key === 'full' ? '#ef4444' : key === 'limited' ? '#f59e0b' : '#22c55e';
            const selected = lot.id === selectedId;
            svg += '<g class="marker" data-lot-id="' + lot.id + '" tabindex="0" role="button" aria-label="' +
                SP.escape(lot.name) + '">';
            if (selected) {
                svg += '<circle cx="' + point.x + '" cy="' + point.y + '" r="18" fill="none" stroke="' + color + '" stroke-opacity="0.35" stroke-width="2"/>';
            }
            svg += '<circle cx="' + point.x + '" cy="' + point.y + '" r="' + (selected ? 11 : 9) + '" fill="' + color + '" opacity="0.22"/>';
            svg += '<circle class="core" cx="' + point.x + '" cy="' + point.y + '" r="' + (selected ? 7 : 6) + '" fill="' + color + '"/>';
            svg += '<text class="map-label" x="' + (point.x + 13) + '" y="' + (point.y + 3.5) + '">' +
                SP.escape(lot.area || lot.name) + ' · ' + lot.availableSlots + ' free</text>';
            svg += '</g>';
        });

        // user marker
        if (origin && origin.lat) {
            const point = project(origin.lat, origin.lng);
            svg += '<g>' +
                '<circle cx="' + point.x + '" cy="' + point.y + '" r="16" fill="rgba(96,165,250,0.18)"/>' +
                '<circle cx="' + point.x + '" cy="' + point.y + '" r="6" fill="#60a5fa" stroke="#fff" stroke-width="2"/>' +
                '<text class="map-label" x="' + (point.x + 12) + '" y="' + (point.y - 10) + '" fill="#bfdbfe">You</text>' +
                '</g>';
        }

        svg += '<text x="' + (width - 14) + '" y="' + (height - 12) + '" text-anchor="end" font-size="10" fill="rgba(255,255,255,0.32)">Simulated map · schematic, not to scale</text>';
        svg += '</svg>';
        mount.innerHTML = svg;

        if (typeof opts.onSelect === 'function') {
            mount.querySelectorAll('[data-lot-id]').forEach(function (node) {
                const id = Number(node.getAttribute('data-lot-id'));
                node.addEventListener('click', function () { opts.onSelect(id); });
                node.addEventListener('keydown', function (event) {
                    if (event.key === 'Enter' || event.key === ' ') {
                        event.preventDefault();
                        opts.onSelect(id);
                    }
                });
            });
        }
    };

    SP.mapLegend = function () {
        return '<span><i class="legend-dot" style="background:#22c55e"></i>Available</span>' +
            '<span><i class="legend-dot" style="background:#f59e0b"></i>Limited</span>' +
            '<span><i class="legend-dot" style="background:#ef4444"></i>Full</span>' +
            '<span><i class="legend-dot" style="background:#60a5fa"></i>Your location</span>';
    };

    /* ---------------------------------------------------------------------
       Boot
       --------------------------------------------------------------------- */
    function markActiveNav() {
        const page = document.body.getAttribute('data-page');
        document.querySelectorAll('.nav a[data-nav]').forEach(function (link) {
            if (link.getAttribute('data-nav') === page) link.classList.add('active');
        });
    }

    function bindGlobalActions() {
        document.addEventListener('click', function (event) {
            if (event.target.closest('[data-open-login]')) {
                event.preventDefault();
                openLogin();
            }
            if (event.target.closest('[data-logout]')) {
                SP.setUser(null);
                SP.toast('Signed out');
            }
            if (event.target.closest('#menu-toggle')) {
                const nav = document.getElementById('site-nav');
                if (nav) nav.classList.toggle('open');
            }
        });
        document.addEventListener('keydown', function (event) {
            if (event.key === 'Escape') closeLogin();
        });
    }

    document.addEventListener('DOMContentLoaded', function () {
        ensureLoginModal();
        updateHeader();
        markActiveNav();
        bindGlobalActions();

        const year = document.querySelector('[data-year]');
        if (year) year.textContent = new Date().getFullYear();

        document.querySelectorAll('[data-open-login]').forEach(function (button) {
            button.addEventListener('click', function (event) {
                event.preventDefault();
                openLogin();
            });
        });
    });

})();
