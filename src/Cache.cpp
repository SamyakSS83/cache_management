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

unsigned int Cache::getSetIndex(unsigned int address) const{
    return (address >> blockOffsetBits) & ((1 << setIndexBits) - 1);
}

unsigned int Cache::getTag(unsigned int address) const{
    return address >> (blockOffsetBits + setIndexBits);
}

unsigned int Cache::getBlockOffset(unsigned int address) const {
    return address & ((1 << blockOffsetBits) - 1);
}

int Cache::findLineInSet(unsigned int setIndex, unsigned int tag) const{
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
            if (sets[setIndex].lines[lineIndex].state == SHARED) {
                // Send invalidation to other caches
                int bytesTransferred = 0;
                for (auto cache : otherCaches) {
                    if (cache->handleBusRequest(BUS_INVALIDATE, address, this, cycle, bytesTransferred)) {
                        busInvalidations++;
                    }
                }
                busTraffic += bytesTransferred;
                
                // Change state to Modified
                sets[setIndex].lines[lineIndex].state = MODIFIED;
                sets[setIndex].lines[lineIndex].dirty = true;
                
                // 1 cycle for the actual write
                totalCycles += 1;
            } 
            else if (sets[setIndex].lines[lineIndex].state == EXCLUSIVE) {
                // Just change state to Modified, no bus transaction needed
                sets[setIndex].lines[lineIndex].state = MODIFIED;
                sets[setIndex].lines[lineIndex].dirty = true;
                
                // 1 cycle for the actual write
                totalCycles += 1;
            }
            else if (sets[setIndex].lines[lineIndex].state == MODIFIED) {
                // Already MODIFIED, just update data
                // 1 cycle for the actual write
                totalCycles += 1;
            }
        }
        
        updateLRU(setIndex, lineIndex);
        return true;
    } else {
        // Cache miss
        missCount++;
        
        // Check if data is in other caches
        int bytesTransferred = 0;
        bool dataInSharedState = false;
        bool dataInModifiedState = false;
        
        // First check if data exists in other caches and in what state
        for (auto cache : otherCaches) {
            unsigned int otherSetIndex = cache->getSetIndex(address);
            unsigned int otherTag = cache->getTag(address);
            int otherLineIndex = cache->findLineInSet(otherSetIndex, otherTag);
            
            if (otherLineIndex != -1 && cache->sets[otherSetIndex].lines[otherLineIndex].state != INVALID) {
                if (cache->sets[otherSetIndex].lines[otherLineIndex].state == SHARED) {
                    dataInSharedState = true;
                } else if (cache->sets[otherSetIndex].lines[otherLineIndex].state == MODIFIED || 
                           cache->sets[otherSetIndex].lines[otherLineIndex].state == EXCLUSIVE) {
                    dataInModifiedState = true;
                    break;  // Most important case
                }
            }
        }
        
        if (op == READ) {
            // Read miss
            if (dataInSharedState) {
                // Data in another cache in S state
                int transferCycles = 2 * (blockSize / 4); // 2n cycles for c2c transfer
                cycle += transferCycles;
                idleCycles += transferCycles;
                busTraffic += blockSize;
                
                // After transfer completes, free the bus and execute the instruction (1 more cycle)
                totalCycles += 1;
            }
            else if (dataInModifiedState) {
                // Data in another cache in E/M state
                int transferCycles = 2 * (blockSize / 4); // 2n cycles for c2c transfer
                cycle += transferCycles;
                idleCycles += transferCycles;
                busTraffic += blockSize;
                
                // Broadcast read request on bus
                for (auto cache : otherCaches) {
                    cache->handleBusRequest(BUS_READ, address, this, cycle, bytesTransferred);
                }
                
                // After transfer completes, free the bus and execute the instruction (1 more cycle)
                totalCycles += 1;
            } 
            else {
                // Not in any cache, fetch from memory
                cycle += 100; // Time to fetch from memory
                idleCycles += 100;
                
                // After memory access completes, free the bus and execute (1 more cycle)
                totalCycles += 1;
            }
        } else {
            // Write miss
            if (dataInSharedState || dataInModifiedState) {
                // First invalidate in other caches (Read with Intent to Modify)
                for (auto cache : otherCaches) {
                    if (cache->handleBusRequest(BUS_INVALIDATE, address, this, cycle, bytesTransferred)) {
                        busInvalidations++;
                    }
                }
                busTraffic += bytesTransferred;
                
                // Then fetch from memory
                cycle += 100; // Time to fetch from memory
                idleCycles += 100;
                
                // After memory access completes, free the bus and execute (1 more cycle)
                totalCycles += 1;
            } else {
                // Not in any cache, fetch from memory
                cycle += 100; // Time to fetch from memory
                idleCycles += 100;
                
                // After memory access completes, free the bus and execute (1 more cycle)
                totalCycles += 1;
            }
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
        
        // Set the new line state based on MESI protocol
        sets[setIndex].lines[lineIndex].valid = true;
        sets[setIndex].lines[lineIndex].tag = tag;
        
        if (op == READ) {
            sets[setIndex].lines[lineIndex].dirty = false;
            sets[setIndex].lines[lineIndex].state = (dataInSharedState || dataInModifiedState) ? SHARED : EXCLUSIVE;
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
                
                // Cache-to-cache transfer timing (2n cycles as per idea.txt)
                int transferCycles = (2 * (blockSize / 4));
                
                // Update the cycle counter (affects when bus is free)
                cycle += transferCycles;
                
                // If requesting cache exists, inform it of the data transfer
                if (requestingCache) {
                    requestingCache->receiveCacheToCache(address, SHARED, transferCycles);
                }
                
                // Writeback to memory (implicit)
                writebackCount++;
                
                // Change state to SHARED
                sets[setIndex].lines[lineIndex].state = SHARED;
                sets[setIndex].lines[lineIndex].dirty = false;
                
                responded = true;
            } 
            else if (sets[setIndex].lines[lineIndex].state == EXCLUSIVE) {
                // Change state to SHARED
                sets[setIndex].lines[lineIndex].state = SHARED;
                
                // Cache-to-cache transfer timing
                int transferCycles = (2 * (blockSize / 4));
                
                // Update the cycle counter
                cycle += transferCycles;
                bytesTransferred += blockSize;
                
                // If requesting cache exists, inform it of the data transfer
                if (requestingCache) {
                    requestingCache->receiveCacheToCache(address, SHARED, transferCycles);
                }
                
                responded = true;
            } 
            else if (sets[setIndex].lines[lineIndex].state == SHARED) {
                // Already shared, just provide the data
                bytesTransferred += blockSize;
                
                // Cache-to-cache transfer timing
                int transferCycles = (2 * (blockSize / 4));
                
                // Update the cycle counter
                cycle += transferCycles;
                
                // If requesting cache exists, inform it of the data transfer
                if (requestingCache) {
                    requestingCache->receiveCacheToCache(address, SHARED, transferCycles);
                }
                
                responded = true;
            }
            break;
            
        case BUS_INVALIDATE:
        case BUS_UPGRADE:
            // Invalidate the line
            sets[setIndex].lines[lineIndex].state = INVALID;
            
            // If the line was modified, write back to memory first
            if (sets[setIndex].lines[lineIndex].dirty) {
                sets[setIndex].lines[lineIndex].dirty = false;
                writebackCount++;
                bytesTransferred += blockSize;
            }
            
            responded = true;
            busInvalidations++;
            break;
            
        case BUS_WRITE:
            // Similar to invalidate but might have additional logic in the future
            sets[setIndex].lines[lineIndex].state = INVALID;
            
            if (sets[setIndex].lines[lineIndex].dirty) {
                sets[setIndex].lines[lineIndex].dirty = false;
                writebackCount++;
                bytesTransferred += blockSize;
            }
            
            responded = true;
            busInvalidations++;
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
            if (cache->sets[setIndex].lines[setIndex].state == MODIFIED) {
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

void Cache::printDebugInfo(MemoryOperation op, unsigned int address, bool isHit, 
                         CacheLineState oldState, CacheLineState newState) {
    std::cout << "Core " << coreId << ": "
              << (op == READ ? "READ" : "WRITE") << " 0x" << std::hex << address << std::dec;
              
    // Print hit/miss status
    std::cout << " - " << (isHit ? "HIT" : "MISS");
    
    // Add details about set and tag
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    std::cout << " [Set: " << setIndex << ", Tag: 0x" << std::hex << tag << std::dec << "]";
    
    // Show state transition if applicable
    if (oldState != INVALID) {
        std::cout << " State: " << stateToString(oldState);
        if (newState != oldState) {
            std::cout << " → " << stateToString(newState);
        }
    } else if (newState != INVALID) {
        std::cout << " New state: " << stateToString(newState);
    }
    
    // Show execution and idle times
    if (isHit) {
        std::cout << " | Exec time: 1 cycle";
    } else if (op == READ) {
        int memTime = (newState == SHARED) ? (2 * (blockSize / 4)) + 1 : 101;
        std::cout << " | Exec time: " << memTime << " cycles";
    } else { // Write miss
        std::cout << " | Exec time: 101 cycles";
    }
    
    std::cout << " | Idle time: " << idleCycles << " cycles" << std::endl;
}
