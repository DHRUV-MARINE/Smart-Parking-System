// ============================================================================
//  graph.cpp - Graph construction + Dijkstra's algorithm.
// ============================================================================
#include "graph.h"

#include <cmath>
#include <limits>

int Graph::addNode() {
    adj_.push_back({});
    return static_cast<int>(adj_.size()) - 1;
}

void Graph::addEdge(int u, int v, double weight, bool twoWay) {
    if (u < 0 || v < 0) return;
    if (u >= nodeCount() || v >= nodeCount()) return;
    adj_[static_cast<std::size_t>(u)].push_back(Edge{v, weight});
    if (twoWay) adj_[static_cast<std::size_t>(v)].push_back(Edge{u, weight});
}

int Graph::edgeCount() const {
    int total = 0;
    for (const auto& list : adj_) total += static_cast<int>(list.size());
    return total / 2;   // every undirected edge is stored twice
}

// ---------------------------------------------------------------------------
// Dijkstra's algorithm.
//
//  1. dist[source] = 0, every other dist = infinity.
//  2. Put the source in a min-heap keyed by the current best distance.
//  3. Repeatedly pop the closest unvisited vertex u ("greedy" step) and relax
//     every edge (u -> v): if dist[u] + w < dist[v], improve dist[v] and
//     push v again.  The stale heap entry is ignored via the visited check.
//
//  Complexity: O((V + E) log V) time, O(V) space.
//  Edges always have a positive weight, which is what makes the greedy choice
//  correct.
// ---------------------------------------------------------------------------
std::vector<double> Graph::dijkstra(int source, std::vector<int>* parent) const {
    const double INF = std::numeric_limits<double>::infinity();
    std::size_t n = adj_.size();
    std::vector<double> dist(n, INF);
    std::vector<bool> visited(n, false);

    if (parent) parent->assign(n, -1);
    if (source < 0 || source >= static_cast<int>(n)) return dist;

    MinHeap heap;
    dist[static_cast<std::size_t>(source)] = 0.0;
    heap.push(0.0, source);

    while (!heap.empty()) {
        MinHeap::Item item = heap.pop();
        int u = item.node;

        // A vertex can be pushed more than once; skip outdated entries.
        if (visited[static_cast<std::size_t>(u)]) continue;
        visited[static_cast<std::size_t>(u)] = true;

        for (const Edge& edge : adj_[static_cast<std::size_t>(u)]) {
            double candidate = dist[static_cast<std::size_t>(u)] + edge.weight;
            if (candidate < dist[static_cast<std::size_t>(edge.to)]) {
                dist[static_cast<std::size_t>(edge.to)] = candidate;
                if (parent) (*parent)[static_cast<std::size_t>(edge.to)] = u;
                heap.push(candidate, edge.to);
            }
        }
    }
    return dist;
}

double haversineKm(double lat1, double lng1, double lat2, double lng2) {
    const double R = 6371.0;                    // Earth radius in km
    const double PI = 3.14159265358979323846;
    double dLat = (lat2 - lat1) * PI / 180.0;
    double dLng = (lng2 - lng1) * PI / 180.0;
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(lat1 * PI / 180.0) * std::cos(lat2 * PI / 180.0) *
               std::sin(dLng / 2) * std::sin(dLng / 2);
    return 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}
