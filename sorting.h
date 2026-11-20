// ============================================================================
//  sorting.h - Sorting + searching algorithms written by hand.
//  (DSA concepts: MERGE SORT, QUICK SORT, BINARY SEARCH)
//
//  We do NOT use std::sort anywhere in the project logic: the point of the
//  academic exercise is to show the algorithm.  std::stable_sort / std::sort
//  appear nowhere in backend/.
// ============================================================================
#pragma once

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Merge sort - stable, O(n log n) time, O(n) extra space.
// Used to sort parking lots by distance, by availability and by name
// (stable order keeps ties in their original order, which is nicer for users).
// ---------------------------------------------------------------------------
template <typename T, typename Compare>
void mergeRange(std::vector<T>& items, std::vector<T>& buffer,
                std::size_t left, std::size_t mid, std::size_t right,
                Compare less) {
    std::size_t i = left, j = mid, k = left;
    while (i < mid && j < right) {
        if (less(items[j], items[i])) buffer[k++] = items[j++];
        else                          buffer[k++] = items[i++];
    }
    while (i < mid)   buffer[k++] = items[i++];
    while (j < right) buffer[k++] = items[j++];
    for (std::size_t x = left; x < right; ++x) items[x] = buffer[x];
}

template <typename T, typename Compare>
void mergeSortRecursive(std::vector<T>& items, std::vector<T>& buffer,
                        std::size_t left, std::size_t right, Compare less) {
    if (right - left < 2) return;
    std::size_t mid = left + (right - left) / 2;
    mergeSortRecursive(items, buffer, left, mid, less);
    mergeSortRecursive(items, buffer, mid, right, less);
    mergeRange(items, buffer, left, mid, right, less);
}

template <typename T, typename Compare>
void mergeSort(std::vector<T>& items, Compare less) {
    if (items.size() < 2) return;
    std::vector<T> buffer(items.size());
    mergeSortRecursive(items, buffer, 0, items.size(), less);
}

// ---------------------------------------------------------------------------
// Quick sort - in-place, average O(n log n), worst case O(n^2).
// Used for "cheapest first" because it is the classic in-place algorithm and
// the comparison is cheap.  Median-of-three pivot avoids the worst case on
// already-sorted input.
// ---------------------------------------------------------------------------
template <typename T, typename Compare>
std::size_t partitionRange(std::vector<T>& items, std::size_t low, std::size_t high,
                           Compare less) {
    std::size_t mid = low + (high - low) / 2;
    // median-of-three
    if (less(items[mid], items[low]))  std::swap(items[mid], items[low]);
    if (less(items[high], items[low])) std::swap(items[high], items[low]);
    if (less(items[high], items[mid])) std::swap(items[high], items[mid]);

    T pivot = items[mid];
    std::swap(items[mid], items[high - 1]);
    std::size_t i = low;
    for (std::size_t j = low; j + 1 < high; ++j) {
        if (less(items[j], pivot)) {
            std::swap(items[i], items[j]);
            ++i;
        }
    }
    std::swap(items[i], items[high - 1]);
    return i;
}

template <typename T, typename Compare>
void quickSortRecursive(std::vector<T>& items, long low, long high, Compare less) {
    if (low >= high) return;
    std::size_t p = partitionRange(items, static_cast<std::size_t>(low),
                                   static_cast<std::size_t>(high), less);
    if (static_cast<long>(p) - 1 > low) quickSortRecursive(items, low, static_cast<long>(p) - 1, less);
    if (static_cast<long>(p) + 1 < high) quickSortRecursive(items, static_cast<long>(p) + 1, high, less);
}

template <typename T, typename Compare>
void quickSort(std::vector<T>& items, Compare less) {
    if (items.size() < 2) return;
    quickSortRecursive(items, 0, static_cast<long>(items.size()) - 1, less);
}

// ---------------------------------------------------------------------------
// Binary search over a name-sorted index.  Returns the position of an exact
// match or -1.  O(log n).
// ---------------------------------------------------------------------------
inline int binarySearchName(const std::vector<std::string>& sortedNames,
                            const std::string& target) {
    int low = 0;
    int high = static_cast<int>(sortedNames.size()) - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        if (sortedNames[mid] == target) return mid;
        if (sortedNames[mid] < target) low = mid + 1;
        else                           high = mid - 1;
    }
    return -1;
}

// Lower bound - first position whose name is >= prefix (used for prefix search).
inline int lowerBoundName(const std::vector<std::string>& sortedNames,
                          const std::string& prefix) {
    int low = 0;
    int high = static_cast<int>(sortedNames.size());
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (sortedNames[mid] < prefix) low = mid + 1;
        else                           high = mid;
    }
    return low;
}

inline std::string toLower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

inline bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}
