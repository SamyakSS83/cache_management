#include "Cache.h"
#include "utils.h"
#include <limits>
#include <cassert>

// Helper: maintain a list of all Cache instances for snooping purposes.
static std::vector<Cache*>& getAllCaches() {
    static std::vector<Cache*> caches;
    return caches;
}

//
// Constructor: also register this cache to the global vector
//
Cache::Cache(int coreId, int s, int E, int b)
    : coreId(coreId), associativity(E), blockSize(1 << b),
      setIndexBits(s), totalCycles(0), readCount(0), writeCount(0),
      missCount(0), hitCount(0), evictionCount(0), writebackCount(0),
      idleCycles(0), busInvalidations(0), busTraffic(0), busRequest(false),
      busGranted(false), stallCycles(0)
{
    // Calculate block offset bits (b bits) assuming blockSize = 2^b
    blockOffsetBits = b;
    
    // Calculate number of sets: 2^s
    numSets = 1 << s;
    
    // For tag bits, assume 32-bit addresses.
    tagBits = 32 - setIndexBits - blockOffsetBits;
    
    // Initialize cache sets.
    for (int i = 0; i < numSets; i++) {
        sets.emplace_back(associativity, blockSize);
    }
    
    // Register this cache so that evictions can look for copies.
    getAllCaches().push_back(this);
}

unsigned int Cache::getSetIndex(unsigned int address) const {
    // Shift right block offset bits then mask with set index mask.
    return (address >> blockOffsetBits) & ((1 << setIndexBits) - 1);
}

unsigned int Cache::getTag(unsigned int address) const {
    // Tag is upper bits after set index and block offset.
    return address >> (blockOffsetBits + setIndexBits);
}

int Cache::findLineInSet(unsigned int setIndex, unsigned int tag) const {
    const CacheSet &cacheSet = sets[setIndex];
    for (int i = 0; i < (int)cacheSet.lines.size(); i++) {
        if (cacheSet.lines[i].valid && cacheSet.lines[i].tag == tag)
            return i;
    }
    return -1;
}

int Cache::pickLRUVictim(unsigned int setIndex) const {
    const CacheSet &cacheSet = sets[setIndex];
    // First choose any invalid line.
    for (int i = 0; i < (int)cacheSet.lines.size(); i++) {
        if (!cacheSet.lines[i].valid) {
            return i;
        }
    }
    // Otherwise, pick the one with smallest lastUsed value.
    int victimIndex = 0;
    unsigned int minUsage = std::numeric_limits<unsigned int>::max();
    for (int i = 0; i < (int)cacheSet.lines.size(); i++) {
        if (cacheSet.lines[i].lastUsed < minUsage) {
            minUsage = cacheSet.lines[i].lastUsed;
            victimIndex = i;
        }
    }
    return victimIndex;
}

void Cache::updateLRU(unsigned int setIndex, int lineIndex) {
    // Increase totalCycles and record usage.
    totalCycles++;
    sets[setIndex].lines[lineIndex].lastUsed = totalCycles;
}

//
// Evict a cache line according to its MESI state and the copies in other caches.
// Modifies the cycle count to account for any stalls due to write-back.
//
void Cache::evictLine(unsigned int setIndex, int lineIndex, int &cycle, int &bytesTransferred) {
    CacheLine &line = sets[setIndex].lines[lineIndex];
    if (!line.valid) return; // Nothing to evict

    // Helper lambda to count copies in other caches
    auto countCopies = [&]() -> int {
        int copies = 0;
        // Scan through all caches registered
        for (Cache* other : getAllCaches()) {
            if (other == this) continue;
            unsigned int otherSet = other->getSetIndex((line.tag << (setIndexBits)) );
            // Use findLineInSet in other caches – here we check every set since
            // we assume same configuration across caches.
            // For simplicity we iterate all sets.
            for (auto &s : other->sets) {
                for (auto &ln : s.lines) {
                    if (ln.valid && ln.tag == line.tag && ln.state != INVALID) {
                        copies++;
                    }
                }
            }
        }
        return copies;
    };

    // Eviction handling according to the state.
    switch (line.state) {
        case INVALID:
            // Nothing to do.
            break;
        case EXCLUSIVE:
            // Straight eviction
            evictionCount++;
            break;
        case SHARED: {
            // Count the number of other copies.
            int copies = countCopies();
            if (copies >= 2) {
                // More than one extra copy; simply evict and count.
                evictionCount++;
            } else if (copies == 1) {
                // Find that one other copy and mark it as EXCLUSIVE.
                for (Cache* other : getAllCaches()) {
                    if (other == this) continue;
                    for (auto &s : other->sets) {
                        for (auto &ln : s.lines) {
                            if (ln.valid && ln.tag == line.tag && ln.state == SHARED) {
                                ln.state = EXCLUSIVE;
                                break;
                            }
                        }
                    }
                }
                evictionCount++;
            } else {
                // No other copies found (should not occur if in SHARED) – simply evict.
                evictionCount++;
            }
            break;
        }
        case MODIFIED:
            // On eviction, a cache line in MODIFIED state must be written back.
            writebackCount++;
            bytesTransferred += blockSize;
            // Stall 100 cycles to perform the write-back.
            cycle += 100;
            // Assume bus ownership for these extra cycles happens within the simulator.
            break;
        default:
            break;
    }
    // Mark line as evicted.
    line.valid = false;
    line.state = INVALID;
}

//
// Process a memory request (read or write).
// If it is a miss and the set is full, pick an LRU victim and evict it.
// Then insert a new line in the proper MESI state: for read, fetched line starts
// with EXCLUSIVE; for write, starts with MODIFIED.
// Returns true if it was a cache hit.
//
bool Cache::processRequest(MemoryOperation op,
                           unsigned int address,
                           int &cycle,
                           std::vector<Cache*> &otherCaches) {
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    int lineIndex = findLineInSet(setIndex, tag);
    
    // Cache hit
    if (lineIndex != -1) {
        updateLRU(setIndex, lineIndex);
        hitCount++;
        // For a write hit, if the line is SHARED, invalidate copies in others.
        if (op == WRITE) {
            if (sets[setIndex].lines[lineIndex].state == SHARED) {
                for (Cache* other : otherCaches) {
                    if (other == this) continue;
                    // Invalidate matching line if present.
                    unsigned int otherSet = other->getSetIndex(address);
                    int otherLine = other->findLineInSet(otherSet, tag);
                    if (otherLine != -1 &&
                        other->sets[otherSet].lines[otherLine].state != INVALID) {
                        other->sets[otherSet].lines[otherLine].state = INVALID;
                        other->busInvalidations++;
                        busTraffic += blockSize;
                    }
                }
                // Upgrade own copy to MODIFIED.
                sets[setIndex].lines[lineIndex].state = MODIFIED;
            }
        }
        // Assume a hit takes 1 cycle.
        cycle += 1;
        totalCycles += 1;
        return true;
    }
    
    // Cache miss
    missCount++;
    
    // If the set is full, pick a victim and evict.
    int victimIndex = pickLRUVictim(setIndex);
    if (sets[setIndex].lines[victimIndex].valid) {
        evictLine(setIndex, victimIndex, cycle, busTraffic);
    }
    
    // Insert the new line.
    CacheLine &line = sets[setIndex].lines[victimIndex];
    line.valid = true;
    line.tag = tag;
    // For a read miss: bring in as EXCLUSIVE; for write miss: directly mark as MODIFIED.
    if (op == READ)
        line.state = EXCLUSIVE;
    else
        line.state = MODIFIED;
    
    updateLRU(setIndex, victimIndex);
    
    // Assume miss penalty: for example, memory fetch might cost 100 cycles.
    cycle += 100 + 1; // 100 for fetch, 1 for processing
    return false;
}

//
// Implement the new processRequest method that tracks cycles and bytes
//
bool Cache::processRequest(MemoryOperation op,
                         unsigned int address,
                         int &cycle,
                         std::vector<Cache*> &otherCaches,
                         int &cyclesUsed,
                         int &bytesTransferred) {
    unsigned int setIndex = getSetIndex(address);
    unsigned int tag = getTag(address);
    int lineIndex = findLineInSet(setIndex, tag);
    int startCycle = cycle;
    bytesTransferred = 0;
    
    // Check if any other cache has this line and in what state
    Cache* ownerCache = nullptr;
    int ownerSetIndex = -1;
    int ownerLineIndex = -1;
    CacheLineState ownerState = INVALID;
    
    for (Cache* other : otherCaches) {
        unsigned int otherSetIndex = other->getSetIndex(address);
        int otherLineIndex = other->findLineInSet(otherSetIndex, tag);
        if (otherLineIndex != -1) {
            CacheLine &otherLine = other->sets[otherSetIndex].lines[otherLineIndex];
            if (otherLine.valid && otherLine.state != INVALID) {
                ownerCache = other;
                ownerSetIndex = otherSetIndex;
                ownerLineIndex = otherLineIndex;
                ownerState = otherLine.state;
                break;
            }
        }
    }
    
    // CASE 1: CACHE HIT
    if (lineIndex != -1) {
        CacheLine &line = sets[setIndex].lines[lineIndex];
        
        // Handle read hit - simple case, always take 1 cycle
        if (op == READ) {
            updateLRU(setIndex, lineIndex);
            hitCount++;
            cycle += 1;
            cyclesUsed = cycle - startCycle;
            return true;
        } 
        // Handle write hit - depends on current state
        else if (op == WRITE) {
            writeCount++;
            hitCount++;
            
            // Write hit on EXCLUSIVE line - simple case, just 1 cycle
            if (line.state == EXCLUSIVE) {
                line.state = MODIFIED;
                updateLRU(setIndex, lineIndex);
                cycle += 1;
                cyclesUsed = cycle - startCycle;
                return true;
            }
            // Write hit on MODIFIED line - also simple, just 1 cycle
            else if (line.state == MODIFIED) {
                updateLRU(setIndex, lineIndex);
                cycle += 1;
                cyclesUsed = cycle - startCycle;
                return true;
            }
            // Write hit on SHARED line - need to invalidate other copies
            else if (line.state == SHARED) {
                // Must wait for bus to be free before proceeding
                // Note: In the actual implementation, busRequest and busGranted
                // flags would be used to model this
                
                // Invalidate all other copies
                for (Cache* other : otherCaches) {
                    unsigned int otherSetIndex = other->getSetIndex(address);
                    int otherLineIndex = other->findLineInSet(otherSetIndex, tag);
                    if (otherLineIndex != -1 && 
                        other->sets[otherSetIndex].lines[otherLineIndex].state != INVALID) {
                        // Mark the other cache's line as invalid
                        other->sets[otherSetIndex].lines[otherLineIndex].state = INVALID;
                        other->busInvalidations++;
                        bytesTransferred += blockSize;
                    }
                }
                
                // Update our line to MODIFIED
                line.state = MODIFIED;
                updateLRU(setIndex, lineIndex);
                
                // Cycle cost: 1 for write + bus acquisition overhead
                cycle += 1;
                cyclesUsed = cycle - startCycle;
                busTraffic += bytesTransferred;
                return true;
            }
        }
    }
    
    // CASE 2: CACHE MISS
    missCount++;
    
    // Handle read miss
    if (op == READ) {
        readCount++;
        
        // If another cache has it in MODIFIED state, we need a cache-to-cache transfer
        if (ownerCache && ownerState == MODIFIED) {
            // First, owner must write back to memory (100 cycles)
            // But it's a cache-to-cache transfer, so data doesn't go to memory first
            ownerCache->writebackCount++; // Still count as a writeback statistically
            
            // Cache-to-cache transfer takes 2*blockSize/4 cycles
            int transferCycles = (2 * blockSize) / 4;
            cycle += transferCycles;
            bytesTransferred += blockSize;
            
            // Change owner's state to SHARED
            ownerCache->sets[ownerSetIndex].lines[ownerLineIndex].state = SHARED;
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in SHARED state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = SHARED;
            updateLRU(setIndex, victimIndex);
            
            // Additional cycle for processing
            cycle += 1;
        }
        // If another cache has it in EXCLUSIVE or SHARED state
        else if (ownerCache && (ownerState == EXCLUSIVE || ownerState == SHARED)) {
            // No need to write back, but still need to transfer
            int transferCycles = (2 * blockSize) / 4;
            cycle += transferCycles;
            bytesTransferred += blockSize;
            
            // Change owner's state to SHARED
            ownerCache->sets[ownerSetIndex].lines[ownerLineIndex].state = SHARED;
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in SHARED state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = SHARED;
            updateLRU(setIndex, victimIndex);
            
            // Additional cycle for processing
            cycle += 1;
        }
        // No other cache has it, fetch from memory
        else {
            // Memory access takes 100 cycles
            cycle += 100;
            bytesTransferred += blockSize;
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in EXCLUSIVE state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = EXCLUSIVE;
            updateLRU(setIndex, victimIndex);
            
            // Additional cycle for processing
            cycle += 1;
        }
    }
    // Handle write miss
    else if (op == WRITE) {
        writeCount++;
        
        // If another cache has it in MODIFIED state
        if (ownerCache && ownerState == MODIFIED) {
            // Owner must write back to memory
            ownerCache->writebackCount++;
            cycle += 100; // 100 cycles for writeback
            
            // Then we fetch from memory
            cycle += 100; // 100 cycles for memory access
            bytesTransferred += 2 * blockSize; // Both operations use bus
            
            // Invalidate the line in owner's cache
            ownerCache->sets[ownerSetIndex].lines[ownerLineIndex].state = INVALID;
            ownerCache->busInvalidations++;
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in MODIFIED state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = MODIFIED;
            updateLRU(setIndex, victimIndex);
            
            // Finally write to it (1 cycle)
            cycle += 1;
        }
        // If another cache has it in EXCLUSIVE or SHARED state
        else if (ownerCache && (ownerState == EXCLUSIVE || ownerState == SHARED)) {
            // Fetch from memory
            cycle += 100; // 100 cycles for memory access
            bytesTransferred += blockSize;
            
            // Invalidate all copies in other caches
            for (Cache* other : otherCaches) {
                unsigned int otherSetIndex = other->getSetIndex(address);
                int otherLineIndex = other->findLineInSet(otherSetIndex, tag);
                if (otherLineIndex != -1 &&
                    other->sets[otherSetIndex].lines[otherLineIndex].state != INVALID) {
                    // Mark the other cache's line as invalid
                    other->sets[otherSetIndex].lines[otherLineIndex].state = INVALID;
                    other->busInvalidations++;
                }
            }
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in MODIFIED state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = MODIFIED;
            updateLRU(setIndex, victimIndex);
            
            // Write to it (1 cycle)
            cycle += 1;
        }
        // No other cache has it, fetch from memory
        else {
            // Fetch from memory
            cycle += 100; // 100 cycles for memory access
            bytesTransferred += blockSize;
            
            // Find a spot in our cache
            int victimIndex = pickLRUVictim(setIndex);
            if (sets[setIndex].lines[victimIndex].valid) {
                evictLine(setIndex, victimIndex, cycle, bytesTransferred);
            }
            
            // Add line to our cache in MODIFIED state
            CacheLine &line = sets[setIndex].lines[victimIndex];
            line.valid = true;
            line.tag = tag;
            line.state = MODIFIED;
            updateLRU(setIndex, victimIndex);
            
            // Write to it (1 cycle)
            cycle += 1;
        }
    }
    
    // Update cycle count and bus traffic
    cyclesUsed = cycle - startCycle;
    busTraffic += bytesTransferred;
    
    return false; // It was a miss
}

//
// Handle snooping bus operations.
// (This implementation is a stub. In a full implementation, this would examine the bus transaction,
// update states accordingly and return whether the request resulted in a write-back.)
//
bool Cache::handleBusRequest(BusTransaction busOp,
                             unsigned int address,
                             Cache* requestor,
                             int &cycle,
                             int &bytesTransferred) {
    // For demonstration, we simply return false.
    return false;
}

//
// Print internal state of the Cache (for debugging purposes).
//
void Cache::printState() {
    std::cout << "Cache state for core " << coreId << ":\n";
    for (unsigned int i = 0; i < sets.size(); i++) {
        std::cout << "Set " << i << ":\n";
        for (unsigned int j = 0; j < sets[i].lines.size(); j++) {
            const CacheLine &line = sets[i].lines[j];
            std::cout << "  Line " << j << ": "
                      << (line.valid ? "Valid" : "Invalid")
                      << ", Tag: " << line.tag
                      << ", State: " << stateToString(line.state)
                      << ", lastUsed: " << line.lastUsed << "\n";
        }
    }
}