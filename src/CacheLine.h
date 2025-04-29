#ifndef CACHE_LINE_H
#define CACHE_LINE_H

#include "utils.h"
#include <vector>

// Structure to represent a cache line
struct CacheLine {
    bool valid;
    bool dirty;
    CacheLineState state;
    unsigned int tag;
    unsigned int lastUsed; // For LRU replacement
    std::vector<unsigned char> data;

    CacheLine(int blockSize) : valid(false), dirty(false), 
                              state(INVALID), tag(0), lastUsed(0) {
        data.resize(blockSize, 0);
    }
};

// Structure to represent a cache set
struct CacheSet {
    std::vector<CacheLine> lines;
    
    CacheSet(int associativity, int blockSize) {
        for (int i = 0; i < associativity; i++) {
            lines.push_back(CacheLine(blockSize));
        }
    }
};

#endif // CACHE_LINE_H
