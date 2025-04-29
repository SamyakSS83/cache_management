#ifndef CACHE_LINE_H
#define CACHE_LINE_H

#include "utils.h"
#include <vector>

// Structure to represent a cache line
struct CacheLine {
    bool valid;              // is this line allocated?
    bool dirty;              // has it been modified?
    CacheLineState state;    // MESI state
    unsigned int tag;
    unsigned int lastUsed;   // for LRU
    std::vector<unsigned char> data;

    // CONTROL SIGNALS
    bool pendingFlush;       // eviction has scheduled a write‐back
    int  pendingStallCycles; // cycles left to stall while flushing

    CacheLine(int blockSize)
      : valid(false)
      , dirty(false)
      , state(INVALID)
      , tag(0)
      , lastUsed(0)
      , pendingFlush(false)
      , pendingStallCycles(0)
    {
        data.resize(blockSize, 0);
    }
};

struct CacheSet {
    std::vector<CacheLine> lines;
    CacheSet(int E, int blockSize) {
        for (int i = 0; i < E; i++)
            lines.emplace_back(blockSize);
    }
};

#endif // CACHE_LINE_H
