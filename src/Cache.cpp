#include "Cache.h"
#include <iostream>
#include <iomanip>

Cache::Cache(int coreId, int s, int E, int b) : coreId(coreId) {
    setIndexBits = s;
    associativity = E;
    blockOffsetBits = b;
    
    numSets = 1 << s;
    blockSize = 1 << b;
    tagBits = 32 - s - b;
    
    // Initialize cache sets
    for (int i = 0; i < numSets; i++) {
        sets.push_back(CacheSet(associativity, blockSize));
    }
    
    // Initialize statistics
    readCount = 0;
    writeCount = 0;
    missCount = 0;
    hitCount = 0;
    evictionCount = 0;
    writebackCount = 0;
    totalCycles = 0;
    idleCycles = 0;
    busInvalidations = 0;
    busTraffic = 0;
}

unsigned int Cache::getSetIndex(unsigned int address) {
    return (address >> blockOffsetBits) & ((1 << setIndexBits) - 1);
}

unsigned int Cache::getTag(unsigned int address) {
    return address >> (blockOffsetBits + setIndexBits);
}

unsigned int Cache::getBlockOffset(unsigned int address) {
    return address & ((1 << blockOffsetBits) - 1);
}

int Cache::findLineInSet(unsigned int setIndex, unsigned int tag) {
    for (int i = 0; i < associativity; i++) {
        if (sets[setIndex].lines[i].valid && sets[setIndex].lines[i].tag == tag) {
            return i;
        }
    }
    return -1; // Not found
}

int Cache::getLRULine(unsigned int setIndex) {
    int lruIndex = 0;
    unsigned int lruValue = sets[setIndex].lines[0].lastUsed;
    
    for (int i = 1; i < associativity; i++) {
        if (sets[setIndex].lines[i].lastUsed < lruValue) {
            lruValue = sets[setIndex].lines[i].lastUsed;
            lruIndex = i;
        }
    }
    
    return lruIndex;
}

void Cache::updateLRU(unsigned int setIndex, int lineIndex) {
    // Set the accessed line's counter to current global cycle
    sets[setIndex].lines[lineIndex].lastUsed = totalCycles;
    
    // No need to update other lines - we just compare their lastUsed values
}

void Cache::evictLine(unsigned int setIndex, int lineIndex, int& cycle) {
    // If the line is dirty, write it back to memory
    if (sets[setIndex].lines[lineIndex].dirty) {
        writebackCount++;
        cycle += 100; // Writeback to memory takes 100 cycles
        idleCycles += 100;
    }
    
    // Mark the line as invalid
    sets[setIndex].lines[lineIndex].valid = false;
    sets[setIndex].lines[lineIndex].dirty = false;
    sets[setIndex].lines[lineIndex].state = INVALID;
}

bool Cache::processRequest(MemoryOperation op, unsigned int address, int& cycle, 
                         std::vector<Cache*>& otherCaches) {
    // Update statistics
    if (op == READ) {
        readCount++;
    } else {
        writeCount++;
    }
    
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    int lineIndex = findLineInSet(setIndex, tag);
    
    if (lineIndex != -1 && sets[setIndex].lines[lineIndex].state != INVALID) {
        // Cache hit
        hitCount++;
        totalCycles += 1; // L1 cache hit is 1 cycle
        
        if (op == READ) {
            // Read hit - no state change needed
        } else { // WRITE
            // Write hit
            if (sets[setIndex].lines[lineIndex].state == SHARED || 
                sets[setIndex].lines[lineIndex].state == EXCLUSIVE) {
                // Need to invalidate copies in other caches (BusUpgrade)
                int bytesTransferred = 0;
                for (auto cache : otherCaches) {
                    if (cache->handleBusRequest(BUS_UPGRADE, address, this, cycle, bytesTransferred)) {
                        busInvalidations++;
                    }
                }
                busTraffic += bytesTransferred;
            }
            
            sets[setIndex].lines[lineIndex].dirty = true;
            sets[setIndex].lines[lineIndex].state = MODIFIED;
        }
        
        updateLRU(setIndex, lineIndex);
        return true;
    } else {
        // Cache miss
        missCount++;
        
        // Check if data is in other caches
        int bytesTransferred = 0;
        bool dataInOtherCache = checkDataInOtherCaches(address, otherCaches, bytesTransferred);
        
        if (dataInOtherCache) {
            // Data comes from another cache - calculate transfer time
            int transferCycles = 2 * (bytesTransferred / 4); // 2 cycles per word
            cycle += transferCycles;
            idleCycles += transferCycles;
            busTraffic += bytesTransferred;
        } else {
            // Data comes from memory
            cycle += 100; // Memory access latency
            idleCycles += 100;
        }
        
        // Find a line to replace
        bool needEviction = true;
        for (int i = 0; i < associativity; i++) {
            if (!sets[setIndex].lines[i].valid || sets[setIndex].lines[i].state == INVALID) {
                lineIndex = i;
                needEviction = false;
                break;
            }
        }
        
        if (needEviction) {
            lineIndex = getLRULine(setIndex);
            evictionCount++;
            evictLine(setIndex, lineIndex, cycle);
        }
        
        // Check if other caches have this block
        bool otherCacheHasBlock = false;
        BusTransaction busOp = (op == READ) ? BUS_READ : BUS_WRITE;
        
        for (auto cache : otherCaches) {
            if (cache->handleBusRequest(busOp, address, this, cycle, bytesTransferred)) {
                otherCacheHasBlock = true;
                if (busOp == BUS_WRITE) {
                    busInvalidations++;
                }
            }
        }
        busTraffic += bytesTransferred;
        
        // Set the new line state based on MESI protocol
        sets[setIndex].lines[lineIndex].valid = true;
        sets[setIndex].lines[lineIndex].tag = tag;
        
        if (op == READ) {
            sets[setIndex].lines[lineIndex].dirty = false;
            sets[setIndex].lines[lineIndex].state = otherCacheHasBlock ? SHARED : EXCLUSIVE;
        } else { // WRITE
            sets[setIndex].lines[lineIndex].dirty = true;
            sets[setIndex].lines[lineIndex].state = MODIFIED;
        }
        
        updateLRU(setIndex, lineIndex);
        return false;
    }
}

bool Cache::handleBusRequest(BusTransaction busOp, unsigned int address, 
                           Cache* requestingCache, int& cycle, int& bytesTransferred) {
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    int lineIndex = findLineInSet(setIndex, tag);
    
    // If the line is not in the cache or is invalid, no action needed
    if (lineIndex == -1 || sets[setIndex].lines[lineIndex].state == INVALID) {
        return false;
    }
    
    bool responded = false;
    
    switch (busOp) {
        case BUS_READ:
            if (sets[setIndex].lines[lineIndex].state == MODIFIED) {
                // Need to provide the modified data to the requesting cache and memory
                bytesTransferred += blockSize; // Transfer entire block
                cycle += 2 * (blockSize / 4); // 2 cycles per word
                
                // Change state to SHARED
                sets[setIndex].lines[lineIndex].state = SHARED;
                sets[setIndex].lines[lineIndex].dirty = false;
                responded = true;
            } else if (sets[setIndex].lines[lineIndex].state == EXCLUSIVE) {
                // Change state to SHARED
                sets[setIndex].lines[lineIndex].state = SHARED;
                responded = true;
            } else if (sets[setIndex].lines[lineIndex].state == SHARED) {
                // Already shared, no state change
                responded = true;
            }
            break;
            
        case BUS_WRITE:
        case BUS_INVALIDATE:
        case BUS_UPGRADE:
            // Invalidate the line
            sets[setIndex].lines[lineIndex].state = INVALID;
            
            // If the line was modified, write back to memory first
            if (sets[setIndex].lines[lineIndex].dirty) {
                sets[setIndex].lines[lineIndex].dirty = false;
                writebackCount++;
                cycle += 100; // Writeback to memory
                idleCycles += 100;
            }
            
            responded = true;
            break;
    }
    
    return responded;
}

bool Cache::checkDataInOtherCaches(unsigned int address, std::vector<Cache*>& otherCaches, 
                                 int& bytesTransferred) {
    bytesTransferred = 0;
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    
    for (auto cache : otherCaches) {
        int lineIndex = cache->findLineInSet(setIndex, tag);
        if (lineIndex != -1 && cache->sets[setIndex].lines[lineIndex].state != INVALID) {
            // Found in another cache
            if (cache->sets[setIndex].lines[lineIndex].state == MODIFIED) {
                bytesTransferred = blockSize;
            }
            return true;
        }
    }
    return false;
}

void Cache::printState() {
    std::cout << "Cache State for Core " << coreId << ":" << std::endl;
    for (unsigned int i = 0; i < sets.size(); i++) {
        std::cout << "Set " << i << ": ";
        for (unsigned int j = 0; j < sets[i].lines.size(); j++) {
            if (sets[i].lines[j].valid) {
                std::cout << "[" << std::hex << sets[i].lines[j].tag 
                          << ":" << stateToString(sets[i].lines[j].state) 
                          << (sets[i].lines[j].dirty ? "D" : " ") << "] ";
            } else {
                std::cout << "[Invalid] ";
            }
        }
        std::cout << std::endl;
    }
    std::cout << std::dec;
}
