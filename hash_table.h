// ============================================================================
//  hash_table.h - Hash table with separate chaining, written from scratch.
//  (DSA concept: HASH TABLE)
//
//  Used for:
//    * parking id      -> position in the locations vector   O(1) average
//    * parking name    -> position in the locations vector   O(1) average
//
//  Average complexity: insert O(1), lookup O(1), remove O(1 + chain length).
//  Worst case O(n) if every key collides - keeping the load factor below 1
//  with automatic rehashing keeps that from happening in practice.
// ============================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

// djb2 string hash (classic, simple, good distribution for short keys).
struct StringHasher {
    static std::size_t hash(const std::string& key) {
        std::size_t h = 5381;
        for (unsigned char c : key) h = h * 33u + c;
        return h;
    }
};

// Knuth multiplicative hash for integers.
struct IntHasher {
    static std::size_t hash(int key) {
        return static_cast<std::size_t>(key) * 2654435761u;
    }
};

template <typename Key, typename Value, typename Hasher>
class HashTable {
public:
    explicit HashTable(std::size_t bucketCount = 17) {
        buckets_.assign(bucketCount < 3 ? 3 : bucketCount, nullptr);
    }

    ~HashTable() { clear(); }

    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Returns false when the key already exists (the old value is replaced).
    bool insert(const Key& key, const Value& value) {
        if (load_factor() > 0.75) rehash(buckets_.size() * 2 + 1);

        std::size_t idx = bucket_of(key);
        for (Node* cur = buckets_[idx]; cur != nullptr; cur = cur->next) {
            if (cur->key == key) { cur->value = value; return false; }
        }
        Node* node = new Node{key, value, buckets_[idx]};
        buckets_[idx] = node;
        ++size_;
        return true;
    }

    // Lookup. Returns true and copies the value into `out` when found.
    bool find(const Key& key, Value& out) const {
        std::size_t idx = bucket_of(key);
        for (Node* cur = buckets_[idx]; cur != nullptr; cur = cur->next) {
            if (cur->key == key) { out = cur->value; return true; }
        }
        return false;
    }

    bool contains(const Key& key) const {
        Value dummy{};
        return find(key, dummy);
    }

    bool remove(const Key& key) {
        std::size_t idx = bucket_of(key);
        Node* prev = nullptr;
        Node* cur = buckets_[idx];
        while (cur != nullptr) {
            if (cur->key == key) {
                if (prev) prev->next = cur->next;
                else buckets_[idx] = cur->next;
                delete cur;
                --size_;
                return true;
            }
            prev = cur;
            cur = cur->next;
        }
        return false;
    }

    std::size_t size() const { return size_; }
    double load_factor() const {
        return static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }

    void clear() {
        for (Node*& head : buckets_) {
            while (head) {
                Node* next = head->next;
                delete head;
                head = next;
            }
        }
        size_ = 0;
    }

private:
    struct Node {
        Key key;
        Value value;
        Node* next;
    };

    std::size_t bucket_of(const Key& key) const {
        return Hasher::hash(key) % buckets_.size();
    }

    void rehash(std::size_t newCount) {
        std::vector<Node*> fresh(newCount, nullptr);
        for (Node* head : buckets_) {
            Node* cur = head;
            while (cur) {
                Node* next = cur->next;
                std::size_t idx = Hasher::hash(cur->key) % newCount;
                cur->next = fresh[idx];
                fresh[idx] = cur;
                cur = next;
            }
        }
        buckets_.swap(fresh);
    }

    std::vector<Node*> buckets_;
    std::size_t size_ = 0;
};
