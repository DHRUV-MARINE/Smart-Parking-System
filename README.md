# SmartParking — Smart Parking Management System

A college-level **Data Structures & Algorithms** project: a working parking website whose
search, sorting, shortest-path, availability and booking logic is implemented in **C++**.
The browser is only a view — every distance, ranking, price and slot count is computed by the
C++ backend and stored in SQLite.

---

## 1. What the website does

| Page | What works |
|---|---|
| `/` (Home) | Landing page, live free-slot stats, nearby parking cards, simulated live map, search bar |
| `/find.html` | Enter an origin (or use geolocation), search by name/area, filter by price / distance / free slots / type, sort by nearest, cheapest, most free or A–Z |
| `/parking.html?id=` | Full details, live slot grid (pick a slot or auto-assign), tiered price list, Dijkstra route from your origin, nearby price comparison, booking panel |
| `/booking.html` | Booking confirmation after a reservation, list of active bookings, cancel a booking |
| `/dashboard.html` | KPIs, occupancy bars, every lot's live status, active bookings |
| `/about.html` | DSA concepts, algorithm explanations, architecture, API reference |

**Booking flow:** choose lot → pick a slot (or auto-assign) → choose hours → estimate is priced by
C++ → sign in (name only, demo) → *Book* → confirmation with reference. Cancelling frees the slot
immediately and promotes the first person on that lot's waiting list.

**Full parking lot:** the lot shows `Full`, booking is disabled, and the user can join a FIFO
waiting list. When someone cancels, the first waiter is dequeued and booked automatically.

---

## 2. Technology stack

* **Backend:** C++17, hand-written HTTP/1.1 server on POSIX sockets / Winsock (no framework).
* **Database:** SQLite (vendored amalgamation in `backend/third_party`, so no separate install).
* **Frontend:** HTML5, CSS3, vanilla JavaScript (no build step, no CDN).
* **Build:** `g++` + `make` (or `build.bat` on Windows).

---

## 3. Project structure

```
SmartParking/
├── frontend/
│   ├── index.html          # landing page
│   ├── find.html           # search + filters + map
│   ├── parking.html        # details, slots, booking
│   ├── booking.html        # confirmation + my bookings
│   ├── dashboard.html      # statistics
│   ├── about.html          # DSA documentation page
│   └── assets/
│       ├── style.css       # design system (navy / white / green)
│       ├── app.js          # shared API wrapper, auth, map, toasts
│       ├── home.js
│       ├── find.js
│       ├── parking.js
│       ├── booking.js
│       └── dashboard.js
├── backend/
│   ├── main.cpp            # seeding, REST routing, static files, --demo mode
│   ├── models.h            # ParkingLocation, Slot, Booking, pricing helpers
│   ├── graph.h/.cpp        # weighted graph, binary min-heap, Dijkstra
│   ├── hash_table.h        # hash table with separate chaining
│   ├── sorting.h           # merge sort, quick sort, insertion sort, binary search
│   ├── parking.h/.cpp      # slot linked lists, waiting queues, search/filter/sort
│   ├── booking.h/.cpp      # booking linked list, cancel + queue promotion
│   ├── database.h/.cpp     # SQLite persistence layer
│   ├── http.h/.cpp         # HTTP server + request parsing
│   └── third_party/        # sqlite3.c / sqlite3.h (amalgamation)
├── database/
│   └── schema.sql          # tables + 10 sample locations + road graph
├── build.bat               # Windows 11 build script
├── Makefile                # Linux / macOS build
└── README.md
```

---

## 4. DSA concepts, implementation and complexity

| # | Concept | Where | Complexity |
|---|---|---|---|
| 1 | **Graph** (adjacency list) | `Graph` in `graph.h/.cpp` — 22 vertices, 36 road edges | `O(V + E)` memory |
| 2 | **Dijkstra** + **priority queue** | `Graph::dijkstra()` with a manually written binary min-heap | `O((V + E) log V)` |
| 3 | **Hash table** (separate chaining) | `HashTable` in `hash_table.h` — parking id → index, name → index | `O(1)` average lookup |
| 4 | **Merge sort** | `sorting.h` — nearest, most-available, A–Z | `O(n log n)` |
| 5 | **Quick sort** | `sorting.h` — cheapest first (median-of-three pivot) | `O(n log n)` average |
| 6 | **Binary search** + lower bound | `ParkingManager::search()` — exact/prefix name search | `O(log n)` |
| 7 | **Linked list** | `SlotList` (slots per lot), `BookingManager` (active bookings) | append `O(1)`, scan `O(n)` |
| 8 | **Queue** (FIFO) | `WaitingQueue` — waiting list per full lot | enqueue/dequeue `O(1)` |
| 9 | **Structures / classes** | `ParkingLocation`, `Slot`, `Booking`, `WaitEntry`, `OriginOption` | — |

`std::sort` / `std::stable_sort` / `std::priority_queue` are **not** used for any project logic.
STL `std::vector` / `std::string` are containers only; the algorithms are hand-written.

### 4.1 Dijkstra — how it works here

1. Every landmark, junction and parking entrance is a vertex (`graph_nodes`).
2. Every road is a two-way weighted edge in kilometres (`graph_edges`).
3. The user's origin (typed place, or geolocation snapped to the nearest vertex) becomes the source.
4. Distances start at 0 for the source and ∞ elsewhere. A min-heap (hand-written `siftUp` /
   `siftDown`) stores `(distance, vertex)`.
5. Pop the closest unvisited vertex, relax all of its edges, push improved distances.
6. The `parent` array rebuilds the actual road route shown on the parking details page.

Because edge weights are non-negative, the greedy choice is correct. One Dijkstra run serves
every parking lot in the result list.

### 4.2 Search and sorting

* Plain text search first attempts an **exact binary search** over the lowercased name index,
  then a **lower-bound prefix scan**, then a substring scan over name, area and address.
* `sort=distance` and `sort=availability` use **merge sort** (stable, keeps ties sane).
* `sort=price` uses **quick sort** with a median-of-three pivot.
* Filters (`maxPrice`, `maxDistance`, `minAvailable`, `type`) are applied in C++ *before* sorting.

### 4.3 Availability and the waiting queue

* Every lot has a **singly linked list** of slots (`SlotList`), loaded from SQLite at start-up.
* Booking takes the requested slot (or the first free one), marks it occupied and writes both the
  slot and the booking to SQLite.
* Cancelling marks the booking cancelled, frees the slot and — if the lot has waiters — dequeues
  the first one and immediately creates a booking for them.

### 4.4 Pricing

`costForHours()` in `models.h`:

```
days        = hours / 24          -> pricePerDay each
rest        = hours % 24
full blocks = rest / 3            -> price3Hours each
remainder                         -> pricePerHour each
```

Example: 5 hours = one 3-hour ticket + 2 hourly tickets. The frontend calls
`/api/compare?ids=..&hours=..` for the live estimate, so pricing is never duplicated in JS.

---

## 5. Database

`schema.sql` creates:

* `parking_locations` — 10 sample lots with name, address, area, coordinates, capacity, tiered
  prices, type, rating, opening hours and graph vertex id.
* `graph_nodes` — 12 landmarks/junctions + 10 parking vertices.
* `graph_edges` — 36 two-way road segments in kilometres.
* `slots` — generated on first run (725 slots) from each lot's capacity and seed occupancy.
* `bookings` — every booking with status `active` / `cancelled`.
* `waiting_list` — FIFO entries for full lots.

Sample data highlights:

| Lot | Area | Slots | ₹/hr | Status at seed |
|---|---|---|---|---|
| The Ridge Parking | The Ridge | 45 | 40 | Available |
| Lakkar Bazaar Parking | Lakkar Bazaar | 40 | 35 | Available |
| Sanjauli Parking | Sanjauli | 50 | 30 | Available |
| ISBT Tutikandi Parking | Tutikandi | 80 | 25 | **Full** |
| Victory Tunnel Parking | Circular Road | 30 | 32 | Available |
| Jakhoo Temple Parking | Jakhoo | 25 | 45 | Limited |
| Chhota Shimla Parking | Chhota Shimla | 40 | 28 | Available |
| New Shimla Parking | New Shimla | 60 | 22 | Available |
| Summer Hill Parking | Summer Hill | 35 | 20 | **Full** (queue demo) |
| Kufri Parking | Kufri | 90 | 18 | Available |

Delete `database/smartparking.db` to regenerate everything from `schema.sql` on the next start.

---

## 6. REST API

| Method | Endpoint | Purpose |
|---|---|---|
| GET | `/api/health` | status, graph size |
| GET | `/api/locations` | search + filter + sort |
| GET | `/api/locations/:id` | details + slots + waiting list |
| GET | `/api/locations/:id/slots` | slot list |
| GET | `/api/locations/:id/route` | Dijkstra route |
| POST | `/api/bookings` | book `{locationId, slotId, hours, user}` |
| GET | `/api/bookings?user=` | a user's active bookings |
| GET | `/api/bookings/:id` | one booking |
| POST | `/api/bookings/:id/cancel` | cancel `{user}` |
| POST | `/api/waitlist` | join waiting list `{locationId, user}` |
| GET | `/api/stats` | dashboard aggregates |
| GET | `/api/compare?ids=1,2&hours=3` | tiered fare comparison |
| GET | `/api/origins` | landmarks for the search box |
| GET | `/api/graph?place=` | graph size + Dijkstra distances |

`GET /api/locations` parameters: `q`, `place`, `lat`, `lng`, `sort` (`distance|price|availability|name`),
`maxPrice`, `maxDistance`, `minAvailable`, `type`, `includeFull`, `limit`.

POST bodies are flat JSON. Responses look like:

```json
{
  "success": true,
  "message": "Slot A-04 booked at The Ridge Parking",
  "booking": { "id": 12, "slotLabel": "A-04", "hours": 3, "amount": 110, "status": "active" },
  "location": { "availableSlots": 26, "totalSlots": 60, "status": "Available" }
}
```

---

## 7. Run on Windows 11 with VS Code

### 7.1 Install the compiler (one time)

1. Install **MSYS2** from <https://www.msys2.org> (or **WinLibs MinGW-w64** from
   <https://winlibs.com>). MSYS2 route:
   ```bat
   winget install MSYS2.MSYS2
   ```
   Then open "MSYS2 UCRT64" and run:
   ```bat
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc
   ```
2. Add the compiler to PATH: `C:\msys64\ucrt64\bin`
   (WinLibs: the folder that contains `g++.exe`).
3. Verify in a **new** terminal:
   ```bat
   g++ --version
   ```
4. Install **VS Code** and the **C/C++** extension (`ms-vscode.cpptools`).

> SQLite does **not** need installing — `backend/third_party/sqlite3.c` is compiled with the project.

### 7.2 Get the project

Unzip / clone the project so that `backend/`, `frontend/`, `database/` and `build.bat` sit in the
same folder, then open that folder in VS Code (`File → Open Folder`).

### 7.3 Build and run

From the VS Code terminal (PowerShell or cmd), in the project root:

```bat
build.bat
smartparking.exe --port 8080
```

Open <http://localhost:8080> in a browser.

Equivalent manual command:

```bat
gcc -O2 -c backend\third_party\sqlite3.c -o sqlite3.o
g++ -std=c++17 -O2 -Ibackend\third_party backend\main.cpp backend\parking.cpp backend\graph.cpp ^
    backend\booking.cpp backend\database.cpp backend\http.cpp sqlite3.o ^
    -o smartparking.exe -lws2_32
smartparking.exe --port 8080
```

### 7.4 VS Code tasks (optional)

`.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "build SmartParking",
      "type": "shell",
      "command": "build.bat",
      "group": { "kind": "build", "isDefault": true }
    },
    {
      "label": "run SmartParking",
      "type": "shell",
      "command": "smartparking.exe --port 8080",
      "dependsOn": "build SmartParking"
    }
  ]
}
```

Press `Ctrl+Shift+B` to build, then run the "run SmartParking" task.

### 7.5 Command-line options

```
--port <n>    HTTP port (default 8080)
--db <file>   SQLite file (default database/smartparking.db)
--demo        print a console DSA demonstration and exit (great for the viva)
```

### 7.6 Linux / macOS

```sh
make
./smartparking --port 8080
make demo
```

---

## 8. Academic demonstration (`--demo`)

`smartparking --demo` prints, in order:

1. Graph size and the five nearest lots from Mall Road with real Dijkstra distances plus the
   rebuilt road route.
2. Hash-table lookup for a valid and an invalid parking id.
3. Every lot sorted cheapest-first (quick sort).
4. Substring search example.
5. A booking, the resulting availability change, and a cancellation restoring it.
6. A full lot's waiting-list size and a FIFO promotion demonstration.

Run it before the viva; it needs no browser.

---

## 9. Testing checklist

* Home page: live stats, nearby cards and map load from `/api/*`.
* Find: origin change, text search, all three sliders, type filter, every sort option, "Use my
  location" (permission prompt), marker click selects the matching card.
* Details: slot grid reflects occupied slots; selecting a slot changes the estimate; booking
  reduces free slots everywhere.
* Booking: confirmation shows the reference; cancelling restores availability; refresh keeps the
  data (SQLite persistence).
* Waiting list: a full lot (e.g. Summer Hill) accepts a join; cancelling the next booking promotes the
  waiter automatically.
* Dashboard: totals match the sum of the lot table; active bookings and revenue update after
  booking/cancel.
* Restart the server: all bookings and availability are still correct.

---

## 10. Notes and known limits

* The map is a **simulated schematic** built in SVG from the coordinates — no external map API or
  key is needed, and marker positions are geographically projected.
* Sign-in is a **demo**: only the name is stored in `localStorage`. There are no passwords, roles or
  server-side sessions, which is acceptable for the academic scope.
* The HTTP server handles one request at a time. That is fine for a lab demo; production would use
  a thread pool or an event loop.
* Seeded availability is generated once; after that the database is the source of truth.
* The waiting list only promotes automatically when a booking is cancelled (not on a timer).

| Column 1 | Column 2 |
| --- | --- |
|  |  |
|  |  |
