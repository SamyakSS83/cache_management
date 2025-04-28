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
    int readCount;
    int writeCount;
    int missCount;
    int hitCount;
    int evictionCount;
    int writebackCount;
    int totalCycles;
    int idleCycles;
    int busInvalidations;
    int busTraffic; // in bytes
    
    // Helper functions
    unsigned int getSetIndex(unsigned int address);
    unsigned int getTag(unsigned int address);
    unsigned int getBlockOffset(unsigned int address);
    int findLineInSet(unsigned int setIndex, unsigned int tag);
    int getLRULine(unsigned int setIndex);
    void updateLRU(unsigned int setIndex, int lineIndex);
    void evictLine(unsigned int setIndex, int lineIndex, int& cycle);

public:
    Cache(int coreId, int s, int E, int b);
    
    // Core cache operations
    bool processRequest(MemoryOperation op, unsigned int address, int& cycle, 
                      std::vector<Cache*>& otherCaches);
    
    // Bus snooping operations
    bool handleBusRequest(BusTransaction busOp, unsigned int address, 
                        Cache* requestingCache, int& cycle, int& bytesTransferred);
    bool checkDataInOtherCaches(unsigned int address, std::vector<Cache*>& otherCaches, 
                               int& bytesTransferred);
    
    // Statistics functions
    int getReadCount() const { return readCount; }
    int getWriteCount() const { return writeCount; }
    int getMissCount() const { return missCount; }
    int getHitCount() const { return hitCount; }
    int getEvictionCount() const { return evictionCount; }
    int getWritebackCount() const { return writebackCount; }
    int getTotalCycles() const { return totalCycles; }
    int getIdleCycles() const { return idleCycles; }
    int getBusInvalidations() const { return busInvalidations; }
    int getBusTraffic() const { return busTraffic; }
    double getMissRate() const { 
        return (readCount + writeCount > 0) ? 
            (double)missCount / (readCount + writeCount) : 0.0; 
    }
    
    // Debug function
    void printState();

    // Add to public methods
    void addIdleTime(int cycles) { idleCycles += cycles; }

    // New method to handle cache-to-cache transfer receipt
    void receiveCacheToCache(unsigned int address, CacheLineState newState, int transferCycles) {
        unsigned int setIndex = getSetIndex(address);
        unsigned int tag = getTag(address);
        int lineIndex = findLineInSet(setIndex, tag);
        
        // Add transfer cycles to idle time
        idleCycles += transferCycles;
        
        // After transfer is complete, update the cache (1 additional cycle)
        totalCycles += 1;
    }
};

#endif // CACHE_H
