#include "CacheSimulator.h"
#include "utils.h"
#include <utility>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <iomanip>

struct CoreState {
    std::ifstream trace;
    std::string currentLine;
    bool finished;
    int extime;    // execution time counter
    int idletime;  // idle time counter
    
    // Simplified cache: maps an address to a MESI state
    std::unordered_map<unsigned int, CacheLineState> cache;
    
    // Statistics
    int totalInstructions;
    int readCount;
    int writeCount;
    int missCount;
    int hitCount;
    int evictionCount;
    int writebackCount;
    int busInvalidations;
    int dataTraffic; // in bytes
};

CacheSimulator::CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                              const std::string& outFileName, bool debug)
    : outFileName(outFileName), debugMode(debug) {
    
    // Store configuration parameters
    setIndexBits = s;
    associativity = E;
    blockBits = b;
    numSets = 1 << s;
    
    numCores = 4; // Quad-core simulation
    totalInvalidations = 0;
    totalBusTraffic = 0;
    totalBusTransactions = 0;
    globalCycle = 0;
    busFree = true;
    busOwner = -1;
    
    // Block size (in bytes) from b bits: blockSize = 2^b
    blockSize = 1 << b;
    
    debugPrint("Initializing simulator with " + std::to_string(numCores) + " cores");
    debugPrint("Block size: " + std::to_string(blockSize) + " bytes");
    
    // Open trace files: one per core
    for (int i = 0; i < numCores; i++) {
        CoreState core;
        std::string fileName = traceFilePrefix + "_proc" + std::to_string(i) + ".trace";
        core.trace.open(fileName);
        if (!core.trace.is_open()) {
            std::cerr << "Error opening trace file: " << fileName << std::endl;
            exit(1);
        }
        // Read the first line if possible
        if (std::getline(core.trace, core.currentLine)) {
            debugPrint("Core " + std::to_string(i) + " first instruction: " + core.currentLine);
        } else {
            core.finished = true;
            debugPrint("Core " + std::to_string(i) + " trace file empty");
        }
        core.finished = false;
        core.extime = 0;
        core.idletime = 0;
        
        // Initialize statistics
        core.totalInstructions = 0;
        core.readCount = 0;
        core.writeCount = 0;
        core.missCount = 0;
        core.hitCount = 0;
        core.evictionCount = 0;
        core.writebackCount = 0;
        core.busInvalidations = 0;
        core.dataTraffic = 0;
        
        cores.push_back(std::move(core));  // Use move semantics here
    }
}

CacheSimulator::~CacheSimulator() {
    // Close trace files
    for (auto &core : cores) {
        if (core.trace.is_open())
            core.trace.close();
    }
}

void CacheSimulator::debugPrint(const std::string& message) {
    if (debugMode) {
        std::cout << "[Cycle " << globalCycle << "] " << message << std::endl;
    }
}

void CacheSimulator::runSimulation() {
    // Continue until every core has finished processing its trace
    while (!std::all_of(cores.begin(), cores.end(), [](const CoreState &cs){ return cs.finished; })) {
        debugPrint("======= Starting cycle " + std::to_string(globalCycle) + " =======");
        if (busFree) {
            debugPrint("Bus is free");
        } else {
            debugPrint("Bus is owned by Core " + std::to_string(busOwner));
        }
        
        // For this cycle, if the bus is free try to give a turn to one core
        bool executed = false;
        for (int coreId = 0; coreId < numCores; coreId++) {
            CoreState &core = cores[coreId];
            if (core.finished) {
                debugPrint("Core " + std::to_string(coreId) + " has finished execution");
                continue;
            }

            // If an instruction is available, process it
            if (!core.currentLine.empty()) {
                // If bus is not free, the core idles
                if (!busFree) {
                    core.idletime++;
                    debugPrint("Core " + std::to_string(coreId) + " is stalled waiting for bus (owner: Core " + 
                              std::to_string(busOwner) + ")");
                    continue;
                }
                
                // Mark bus busy for this core
                busFree = false;
                busOwner = coreId;
                debugPrint("Core " + std::to_string(coreId) + " acquired bus");
                
                std::istringstream iss(core.currentLine);
                char op;
                std::string addrStr;
                iss >> op >> addrStr;
                unsigned int address = std::stoul(addrStr, nullptr, 16);
                
                core.totalInstructions++;
                debugPrint("Core " + std::to_string(coreId) + " processing: " + op + " " + addrStr);
                
                // Process read instruction
                if (op == 'R') {
                    core.readCount++;
                    // Read Hit: if the address is in the core's cache and state is not INVALID
                    if (core.cache.find(address) != core.cache.end() &&
                        core.cache[address] != INVALID) {
                        // Read hit: execute in 1 cycle
                        core.hitCount++;
                        core.extime += 1;
                        globalCycle += 1;
                        
                        CacheLineState oldState = core.cache[address];
                        debugPrint("Core " + std::to_string(coreId) + " READ HIT for address " + 
                                  addrStr + " (state: " + stateToString(oldState) + ")");
                    } else {
                        // Read Miss
                        core.missCount++;
                        debugPrint("Core " + std::to_string(coreId) + " READ MISS for address " + addrStr);
                        
                        // First check if any other core has the address
                        bool foundInOther = false;
                        int ownerCore = -1;
                        CacheLineState otherState = INVALID;
                        
                        for (int j = 0; j < numCores; j++) {
                            if (j == coreId) continue;
                            if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                cores[j].cache[address] != INVALID) {
                                foundInOther = true;
                                otherState = cores[j].cache[address];
                                ownerCore = j;
                                break;
                            }
                        }
                        
                        totalBusTransactions++;
                        
                        if (foundInOther) {
                            // Cache-to-cache transfer
                            int transferCycles = 2 * (blockSize / 4); // 2n cycles where n = blockSize/4
                            
                            debugPrint("Core " + std::to_string(coreId) + " found data in Core " + 
                                      std::to_string(ownerCore) + " (state: " + stateToString(otherState) + ")");
                            
                            // Extra cycle when data comes from an E or M state
                            if (otherState == EXCLUSIVE || otherState == MODIFIED) {
                                transferCycles += 1;
                                debugPrint("Extra cycle for E/M state transfer");
                            }
                            
                            core.extime += transferCycles + 1; // +1 for execution after transfer
                            globalCycle += transferCycles + 1;
                            
                            // After transfer, mark line in requester as SHARED
                            core.cache[address] = SHARED;
                            
                            // If source was E or M, change it to S too
                            if (otherState == EXCLUSIVE || otherState == MODIFIED) {
                                cores[ownerCore].cache[address] = SHARED;
                                debugPrint("Core " + std::to_string(ownerCore) + " state changed to SHARED");
                            }
                            
                            // Update bus traffic statistics
                            core.dataTraffic += blockSize;
                            totalBusTraffic += blockSize;
                            
                            debugPrint("Cache-to-cache transfer complete (took " + 
                                      std::to_string(transferCycles) + " cycles)");
                            debugPrint("Core " + std::to_string(coreId) + " state now SHARED");
                        } else {
                            // Data not found in any other cache: fetch from memory
                            int memAccessCycles = 100;
                            core.extime += memAccessCycles + 1; // +1 for execute cycle
                            globalCycle += memAccessCycles + 1;
                            
                            // Set state to EXCLUSIVE
                            core.cache[address] = EXCLUSIVE;
                            
                            // Update bus traffic
                            core.dataTraffic += blockSize;
                            totalBusTraffic += blockSize;
                            
                            debugPrint("Memory fetch complete (took " + 
                                      std::to_string(memAccessCycles) + " cycles)");
                            debugPrint("Core " + std::to_string(coreId) + " state now EXCLUSIVE");
                        }
                    }
                }
                // Process write instruction
                else if (op == 'W') {
                    core.writeCount++;
                    // Write Hit
                    if (core.cache.find(address) != core.cache.end() &&
                        core.cache[address] != INVALID) {
                        core.hitCount++;
                        CacheLineState oldState = core.cache[address];
                        
                        debugPrint("Core " + std::to_string(coreId) + " WRITE HIT for address " + 
                                  addrStr + " (state: " + stateToString(oldState) + ")");
                        
                        // If state is SHARED, send invalidations to other cores
                        if (oldState == SHARED) {
                            debugPrint("Sending invalidations to other cores with SHARED copies");
                            totalBusTransactions++;
                            
                            for (int j = 0; j < numCores; j++) {
                                if (j == coreId) continue;
                                if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                    cores[j].cache[address] != INVALID) {
                                    // Invalidate other core's copy
                                    CacheLineState prevState = cores[j].cache[address];
                                    cores[j].cache[address] = INVALID;
                                    totalInvalidations++;
                                    cores[j].busInvalidations++;
                                    core.dataTraffic += blockSize;
                                    totalBusTraffic += blockSize;
                                    
                                    debugPrint("Invalidated Core " + std::to_string(j) + 
                                              " copy (was " + stateToString(prevState) + ")");
                                }
                            }
                            // Then update own state to MODIFIED
                            core.cache[address] = MODIFIED;
                            core.extime += 1;
                            globalCycle += 1;
                            
                            debugPrint("Core " + std::to_string(coreId) + " state changed to MODIFIED");
                        } else if (oldState == EXCLUSIVE) {
                            // Write hit in EXCLUSIVE state takes 1 cycle, becomes MODIFIED
                            core.extime += 1;
                            globalCycle += 1;
                            core.cache[address] = MODIFIED;
                            
                            debugPrint("Core " + std::to_string(coreId) + 
                                      " state changed from EXCLUSIVE to MODIFIED");
                        } else if (oldState == MODIFIED) {
                            // Write hit in MODIFIED state takes 1 cycle
                            core.extime += 1;
                            globalCycle += 1;
                            
                            debugPrint("Core " + std::to_string(coreId) + 
                                      " remains in MODIFIED state");
                        }
                    } else {
                        // Write Miss
                        core.missCount++;
                        debugPrint("Core " + std::to_string(coreId) + " WRITE MISS for address " + addrStr);
                        totalBusTransactions++;
                        
                        // Invalidate copies from other caches, if any
                        bool foundInOther = false;
                        for (int j = 0; j < numCores; j++) {
                            if (j == coreId) continue;
                            if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                cores[j].cache[address] != INVALID) {
                                foundInOther = true;
                                CacheLineState prevState = cores[j].cache[address];
                                cores[j].cache[address] = INVALID;
                                totalInvalidations++;
                                cores[j].busInvalidations++;
                                core.dataTraffic += blockSize;
                                totalBusTraffic += blockSize;
                                
                                debugPrint("Invalidated Core " + std::to_string(j) + 
                                          " copy (was " + stateToString(prevState) + ")");
                            }
                        }
                        
                        // Fetch data from memory
                        int memAccessCycles = 100;
                        core.extime += memAccessCycles + 1;
                        globalCycle += memAccessCycles + 1;
                        
                        // Update bus traffic for memory fetch
                        core.dataTraffic += blockSize;
                        totalBusTraffic += blockSize;
                        
                        // After write miss, update own state to MODIFIED
                        core.cache[address] = MODIFIED;
                        
                        debugPrint("Memory fetch and modify complete (took " + 
                                  std::to_string(memAccessCycles+1) + " cycles)");
                        debugPrint("Core " + std::to_string(coreId) + " state now MODIFIED");
                    }
                }
                
                // Mark bus as free
                busFree = true;
                debugPrint("Core " + std::to_string(coreId) + " released the bus");
                busOwner = -1;
                executed = true;
                
                // Read next line for this core
                if (!std::getline(core.trace, core.currentLine)) {
                    core.finished = true;
                    debugPrint("Core " + std::to_string(coreId) + " has no more instructions");
                } else {
                    debugPrint("Core " + std::to_string(coreId) + " next instruction: " + core.currentLine);
                }
            }
        }
        
        // If no core executed (e.g., all waiting for the bus), advance the clock
        if (!executed) {
            globalCycle++;
            debugPrint("No execution this cycle, advancing global clock");
            for (int coreId = 0; coreId < numCores; coreId++) {
                if (!cores[coreId].finished) {
                    cores[coreId].idletime++;
                    debugPrint("Core " + std::to_string(coreId) + " idle time increased");
                }
            }
        }
    }
    
    printStatistics();
}

//
// Print simulation statistics according to the requested format
//
void CacheSimulator::printStatistics() {
    std::ofstream outFile;
    if (!outFileName.empty()) {
        outFile.open(outFileName);
    }
    
    std::ostream &out = (outFile.is_open() ? outFile : std::cout);
    
    // Calculate cache size in KB
    double cacheSize = (double)(numSets * associativity * blockSize) / 1024.0;
    
    // Print simulation parameters
    out << "Simulation Parameters:" << std::endl;
    out << "Trace Prefix: " << "app" << std::endl;  // Placeholder, replace with actual prefix
    out << "Set Index Bits: " << setIndexBits << std::endl;
    out << "Associativity: " << associativity << std::endl;
    out << "Block Bits: " << blockBits << std::endl;
    out << "Block Size (Bytes): " << blockSize << std::endl;
    out << "Number of Sets: " << numSets << std::endl;
    out << "Cache Size (KB per core): " << std::fixed << std::setprecision(2) << cacheSize << std::endl;
    out << "MESI Protocol: Enabled" << std::endl;
    out << "Write Policy: Write-back, Write-allocate" << std::endl;
    out << "Replacement Policy: LRU" << std::endl;
    out << "Bus: Central snooping bus" << std::endl;
    out << std::endl;
    
    // Core statistics
    for (int i = 0; i < numCores; i++) {
        const CoreState &core = cores[i];
        
        double missRate = 0.0;
        if (core.readCount + core.writeCount > 0) {
            missRate = 100.0 * (double)core.missCount / (double)(core.readCount + core.writeCount);
        }
        
        out << "Core " << i << " Statistics:" << std::endl;
        out << "Total Instructions: " << core.totalInstructions << std::endl;
        out << "Total Reads: " << core.readCount << std::endl;
        out << "Total Writes: " << core.writeCount << std::endl;
        out << "Total Execution Cycles: " << core.extime << std::endl;
        out << "Idle Cycles: " << core.idletime << std::endl;
        out << "Cache Misses: " << core.missCount << std::endl;
        out << "Cache Miss Rate: " << std::fixed << std::setprecision(2) << missRate << "%" << std::endl;
        out << "Cache Evictions: " << core.evictionCount << std::endl;
        out << "Writebacks: " << core.writebackCount << std::endl;
        out << "Bus Invalidations: " << core.busInvalidations << std::endl;
        out << "Data Traffic (Bytes): " << core.dataTraffic << std::endl;
        out << std::endl;
    }
    
    // Overall bus summary
    out << "Overall Bus Summary:" << std::endl;
    out << "Total Bus Transactions: " << totalBusTransactions << std::endl;
    out << "Total Bus Traffic (Bytes): " << totalBusTraffic << std::endl;
    
    if (outFile.is_open()) {
        outFile.close();
    }
}