#include "CacheSimulator.h"
#include "utils.h"
#include "Cache.h"
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
using namespace std;

// Extend the CoreState structure to include a pointer to the per‐core cache.
struct CoreState {
    std::ifstream trace;
    std::string currentLine;
    bool finished;
    int extime;    // execution time counter
    int idletime;  // idle time counter
    int finishtime; // finish time counter

    // Each core now uses a Cache (with full LRU and MESI handling)
    Cache* cachePtr;
    
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
    : outFileName(outFileName), debugMode(debug)
{
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
    
    // Block size in bytes: blockSize = 2^b
    blockSize = 1 << b;
    
    debugPrint("Initializing simulator with " + std::to_string(numCores) + " cores");
    debugPrint("Block size: " + std::to_string(blockSize) + " bytes");
    
    // Open trace files and create a corresponding Cache for each core.
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
        
        // Create the per‐core cache. (Each Cache now internally implements LRU and the detailed MESI logic.)
        core.cachePtr = new Cache(i, s, E, b);
        
        cores.push_back(std::move(core));  // Use move semantics here
    }
}

CacheSimulator::~CacheSimulator() {
    // Close trace files and cleanup each core's cache
    for (auto &core : cores) {
        if (core.trace.is_open())
            core.trace.close();
        delete core.cachePtr;
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
        
        // For this cycle, if the bus is free we try to give a turn to one core.
        bool executed = false;
        for (int coreId = 0; coreId < numCores; coreId++) {
            CoreState &core = cores[coreId];
            if (core.finished) {
                debugPrint("Core " + std::to_string(coreId) + " has finished execution");
                continue;
            }
            
            if (!core.currentLine.empty()) {
                // If the bus is not free, the core idles.
                // if (!busFree) {
                //     core.idletime++;
                //     debugPrint("Core " + std::to_string(coreId) + " is stalled waiting for bus (owner: Core " +
                //                std::to_string(busOwner) + ")");
                //     continue;
                // }
                
                // Mark bus busy for this core.
                // busFree = false;
                // busOwner = coreId;
                // debugPrint("Core " + std::to_string(coreId) + " acquired bus");
                
                // Use an istringstream to parse the line command from the trace
                std::istringstream iss(core.currentLine);
                char opChar;
                std::string addrStr;
                iss >> opChar >> addrStr;
                unsigned int address = std::stoul(addrStr, nullptr, 16);
                
                core.totalInstructions++;
                debugPrint("Core " + std::to_string(coreId) + " processing: " + opChar + " " + addrStr);
                
                // Build a vector of other Cache pointers (for snooping/invalidation) for LRU/MESI handling.
                std::vector<Cache*> otherCaches;
                for (int j = 0; j < numCores; j++) {
                    if (j != coreId)
                        otherCaches.push_back(cores[j].cachePtr);
                }
                
                // Process the instruction using the Cache's processRequest which integrates LRU and MESI logic.
                bool hit = false;
                int cyclesUsed = 0;
                int bytesTransferred = 0;
                
                if (opChar == 'R') {
                    core.readCount++;

                    unsigned int setIndex = core.cachePtr->getSetIndex(address);
                    unsigned int tag = core.cachePtr->getTag(address);
                    int lineIndex = core.cachePtr->findLineInSet(setIndex, tag);
                    std::cout << "coreId: " << coreId << " lineIndex : " << lineIndex << " address: " << address << std::endl;

                    if (lineIndex == -1) { //miss happened
                        if (!busFree) { //bus not free, stall on read miss
                            core.idletime++;
                            cout << "hee hie ihei" << endl;
                            debugPrint("Core " + std::to_string(coreId) + " is stalled waiting for bus (owner: Core " +
                                       std::to_string(busOwner) + ")");
                            continue;
                        }
                    }
                    hit = core.cachePtr->processRequest(READ, address, globalCycle, otherCaches, cyclesUsed, bytesTransferred);

                    std::cout << "coreId: " << coreId << " hit : " << hit << " address: " << address << std::endl;
                    cout << "bus is free at cycle: " << globalCycle << " busOwner: " << busOwner << endl;

                    if (!hit) {
                        busFree = false;
                        busOwner = coreId;
                        debugPrint("Core " + std::to_string(coreId) + " acquired bus");
                    }

                    core.extime += cyclesUsed;
                    core.dataTraffic += bytesTransferred;
                    totalBusTraffic += bytesTransferred;
                    
                    if (bytesTransferred > 0) {
                        totalBusTransactions++;
                    }
                    
                    if (hit) {
                        debugPrint("Core " + std::to_string(coreId) + " READ HIT for address " + addrStr);
                    } else {
                        debugPrint("Core " + std::to_string(coreId) + " READ MISS for address " + addrStr);
                    }
                }
                else if (opChar == 'W') {
                    core.writeCount++;
                    hit = core.cachePtr->processRequest(WRITE, address, globalCycle, otherCaches, cyclesUsed, bytesTransferred);
                    core.extime += cyclesUsed;
                    core.dataTraffic += bytesTransferred;
                    totalBusTraffic += bytesTransferred;
                    
                    if (bytesTransferred > 0) {
                        totalBusTransactions++;
                    }
                    
                    if (hit) {
                        debugPrint("Core " + std::to_string(coreId) + " WRITE HIT for address " + addrStr);
                    } else {
                        debugPrint("Core " + std::to_string(coreId) + " WRITE MISS for address " + addrStr);
                    }
                }
                
                // Update hit/miss statistics based on the returned result.
                if (hit)
                    core.hitCount++;
                else
                    core.missCount++;
                
                // Also update eviction, write-back counts and invalidations from the cache
                core.evictionCount = core.cachePtr->getEvictionCount();
                core.writebackCount = core.cachePtr->getWritebackCount();
                core.busInvalidations = core.cachePtr->getBusInvalidations();
                
                // Release the bus.
                busFree = true;
                busOwner = -1;
                debugPrint("Core " + std::to_string(coreId) + " released the bus");
                executed = true;
                
                // In debug mode, print this core's cache state after processing the instruction.
                if (debugMode) {
                    debugPrint("Core " + std::to_string(coreId) + " cache state:");
                    core.cachePtr->printState();
                }
                
                // Read next line for this core.
                if (!std::getline(core.trace, core.currentLine)) {
                    core.finished = true;
                    core.finishtime = globalCycle;
                    debugPrint("Core " + std::to_string(coreId) + " has no more instructions");
                } else {
                    debugPrint("Core " + std::to_string(coreId) + " next instruction: " + core.currentLine);
                }
            } // end processing current line
        } // end for all cores
        
        // If no core executed in this cycle, simply advance the global clock.
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
    } // end simulation while loop
    
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
    
    // Calculate cache size in KB (per core)
    double cacheSize = (double)(numSets * associativity * blockSize) / 1024.0;
    
    // Print simulation parameters.
    out << "Simulation Parameters:" << std::endl;
    out << "Trace Prefix: " << "app" << std::endl;  // Replace with actual prefix if needed.
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
    
    // Core statistics.
    for (int i = 0; i < numCores; i++) {
        const CoreState &core = cores[i];
        
        double missRate = 0.0;
        if (core.readCount + core.writeCount > 0)
            missRate = 100.0 * (double)core.missCount / (double)(core.readCount + core.writeCount);
        
        out << "Core " << i << " Statistics:" << std::endl;
        out << "Total Instructions: " << core.totalInstructions << std::endl;
        out << "Total Reads: " << core.readCount << std::endl;
        out << "Total Writes: " << core.writeCount << std::endl;
        out << "Total Execution Cycles: " << core.extime << std::endl;
        out << "Idle Cycles: " << core.finishtime - core.extime << std::endl;
        out << "Cache Misses: " << core.missCount << std::endl;
        out << "Cache Miss Rate: " << std::fixed << std::setprecision(2) << missRate << "%" << std::endl;
        out << "Cache Evictions: " << core.evictionCount << std::endl;
        out << "Writebacks: " << core.writebackCount << std::endl;
        out << "Bus Invalidations: " << core.busInvalidations << std::endl;
        out << "Data Traffic (Bytes): " << core.dataTraffic << std::endl;
        out << std::endl;
    }
    
    // Overall bus summary.
    out << "Overall Bus Summary:" << std::endl;
    out << "Total Bus Transactions: " << totalBusTransactions << std::endl;
    out << "Total Bus Traffic (Bytes): " << totalBusTraffic << std::endl;
    
    if (outFile.is_open())
        outFile.close();
}