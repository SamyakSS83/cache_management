#ifndef CACHE_H
#define CACHE_H

#include "utils.h"
#include "CacheLine.h"
#include <vector>

class Cache {
private:
    int coreId;
    int numSets;
    int associativity;
    int blockSize;
    int blockOffsetBits;
    int setIndexBits;
    int tagBits;
    std::vector<CacheSet> sets;

    // Statistics
    int readCount, writeCount, missCount, hitCount;
    int evictionCount, writebackCount;
    int totalCycles, idleCycles;
    int busInvalidations, busTraffic;

    // CONTROL SIGNALS
    bool busRequest;        // this cache wants the bus
    bool busGranted;        // bus has been granted
    int  stallCycles;       // cycles to wait (e.g. for flush)

    // Helpers
    
    public:
    unsigned int getSetIndex(unsigned int address) const;
    unsigned int getTag(unsigned int address) const;
    int findLineInSet(unsigned int setIndex, unsigned int tag) const;
    int pickLRUVictim(unsigned int setIndex) const;
    void updateLRU(unsigned int setIndex, int lineIndex);
    void evictLine(unsigned int setIndex, int lineIndex, int &cycle);
    Cache(int coreId, int s, int E, int b);

    // processes a core read / write
    bool processRequest(MemoryOperation op,
                        unsigned int address,
                        int &cycle,
                        std::vector<Cache*> &otherCaches);

    // snooping bus operations
    bool handleBusRequest(BusTransaction busOp,
                          unsigned int address,
                          Cache* requestor,
                          int &cycle,
                          int &bytesTransferred);

    // statistics getters ...
    int getMissCount() const { return missCount; }
    int getEvictionCount() const { return evictionCount; }
    int getWritebackCount() const { return writebackCount; }

    // for debug / tracing
    void printState();
};

#endif // CACHE_H
