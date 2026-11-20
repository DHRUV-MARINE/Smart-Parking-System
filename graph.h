// ============================================================================
//  graph.h - Weighted road graph + Dijkstra's shortest path.
//  (DSA concepts: GRAPH (adjacency list), PRIORITY QUEUE (binary min-heap),
//   DIJKSTRA'S ALGORITHM)
//
//  Vertices  : landmarks, junctions and parking lots.
//  Edges     : two-way road segments with a distance in kilometres.
//  Complexity: Dijkstra with a binary heap is O((V + E) log V).
// ============================================================================
#pragma once

#include <vector>

// An edge in the adjacency list.
struct Edge {
    int to = -1;
    double weight = 0.0;   // kilometres
};

class Graph {
public:
    Graph() = default;

    int addNode();                                       // returns the new id
    void addEdge(int u, int v, double weight, bool twoWay = true);
    int nodeCount() const { return static_cast<int>(adj_.size()); }
    int edgeCount() const;

    // Dijkstra from `source`. Returns a distance vector (infinity if
    // unreachable). If `parent` is given it is filled with the previous node
    // of every shortest path, so the actual route can be rebuilt.
    std::vector<double> dijkstra(int source, std::vector<int>* parent = nullptr) const;

private:
    // ---- Binary min-heap used as the priority queue inside Dijkstra ----
    // Written by hand (no std::priority_queue) so the sift-up / sift-down
    // mechanics can be explained in the viva.
    class MinHeap {
    public:
        struct Item { double dist; int node; };

        bool empty() const { return data_.empty(); }
        std::size_t size() const { return data_.size(); }

        void push(double dist, int node) {
            data_.push_back(Item{dist, node});
            siftUp(static_cast<long>(data_.size()) - 1);
        }

        Item pop() {
            Item top = data_.front();
            data_.front() = data_.back();
            data_.pop_back();
            if (!data_.empty()) siftDown(0);
            return top;
        }

    private:
        static bool before(const Item& a, const Item& b) {
            if (a.dist != b.dist) return a.dist < b.dist;
            return a.node < b.node;
        }
        void siftUp(long i) {
            while (i > 0) {
                long parent = (i - 1) / 2;
                if (!before(data_[static_cast<std::size_t>(i)],
                            data_[static_cast<std::size_t>(parent)])) break;
                std::swap(data_[static_cast<std::size_t>(i)],
                          data_[static_cast<std::size_t>(parent)]);
                i = parent;
            }
        }
        void siftDown(long i) {
            long n = static_cast<long>(data_.size());
            while (true) {
                long left = 2 * i + 1, right = 2 * i + 2, best = i;
                if (left < n && before(data_[static_cast<std::size_t>(left)],
                                       data_[static_cast<std::size_t>(best)])) best = left;
                if (right < n && before(data_[static_cast<std::size_t>(right)],
                                        data_[static_cast<std::size_t>(best)])) best = right;
                if (best == i) break;
                std::swap(data_[static_cast<std::size_t>(i)],
                          data_[static_cast<std::size_t>(best)]);
                i = best;
            }
        }

        std::vector<Item> data_;
    };

    std::vector<std::vector<Edge>> adj_;
};

// Great-circle distance between two coordinates (used only when the browser
// sends a raw latitude/longitude and we must snap it to the nearest graph
// vertex - the shortest path itself always comes from Dijkstra).
double haversineKm(double lat1, double lng1, double lat2, double lng2);
