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
#include <bits/stdc++.h>

struct CoreState {
    std::ifstream trace;
    std::string currentLine;
    bool finished;
    int extime;    // execution time counter
    int idletime;  // idle time counter
    // Simplified cache: maps an address to a MESI state.
    std::unordered_map<unsigned int, CacheLineState> cache;
};

CacheSimulator::CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                              const std::string& outFileName)
    : outFileName(outFileName) {
    numCores = 4; // Quad-core simulation
    totalInvalidations = 0;
    totalBusTraffic = 0;
    globalCycle = 0;
    busFree = true;
    busOwner = -1;
    
    // Block size (in bytes) from b bits: blockSize = 2^b
    blockSize = 1 << b;
    
    // Open six trace files: one per core.
    for (int i = 0; i < numCores; i++) {
        CoreState core;
        std::string fileName = traceFilePrefix + "_proc" + std::to_string(i) + ".trace";
        core.trace.open(fileName);
        if (!core.trace.is_open()) {
            std::cerr << "Error opening trace file: " << fileName << std::endl;
            exit(1);
        }
        // Read the first line if possible.
        if (std::getline(core.trace, core.currentLine)) {
            // ready to simulate
        } else {
            core.finished = true;
        }
        core.finished = false;
        core.extime = 0;
        core.idletime = 0;
        cores.push_back(std::move(core));  // Use move semantics here.
    }
}

CacheSimulator::~CacheSimulator() {
    // Close trace files.
    for (auto &core : cores) {
        if (core.trace.is_open())
            core.trace.close();
    }
}

//
// New simulation loop that uses a single global clock and per–core extime and idletime counters.
// This implementation takes inspiration from your idea.txt pseudo code.
// Note: You may fine–tune or extend timing logic as needed.
//
void CacheSimulator::runSimulation() {
    // Continue until every core has finished processing its trace.
    while (!std::all_of(cores.begin(), cores.end(), [](const CoreState &cs){ return cs.finished; })) {
        // For this cycle, if the bus is free try to give a turn to one core.
        bool executed = false;
        for (int coreId = 0; coreId < numCores; coreId++) {
            CoreState &core = cores[coreId];
            if (core.finished)
                continue;

            // If an instruction is available, process it.
            if (!core.currentLine.empty()) {
                // If bus is not free, the core idles.
                if (!busFree) {
                    core.idletime++;
                    globalCycle++;
                    continue;
                }
                // Mark bus busy for this core.
                busFree = false;
                busOwner = coreId;
                
                std::istringstream iss(core.currentLine);
                char op;
                std::string addrStr;
                iss >> op >> addrStr;
                unsigned int address = std::stoul(addrStr, nullptr, 16);
                
                // Process read instruction.
                if (op == 'R') {
                    // Read Hit: if the address is in the core's cache and state is not INVALID.
                    if (core.cache.find(address) != core.cache.end() &&
                        core.cache[address] != INVALID) {
                        // Read hit: execute in 1 cycle.
                        core.extime += 1;
                        globalCycle += 1;
                    } else {
                        // Read Miss.
                        // First check if any other core has the address.
                        bool foundInOther = false;
                        CacheLineState otherState = INVALID;
                        for (int j = 0; j < numCores; j++) {
                            if (j == coreId) continue;
                            if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                cores[j].cache[address] != INVALID) {
                                foundInOther = true;
                                otherState = cores[j].cache[address];
                                break;
                            }
                        }
                        
                        if (foundInOther) {
                            // Cache-to-cache transfer.
                            // Assume transfer time = 2n cycles where n = blockSize/4
                            int transferCycles = 2 * (blockSize / 4);
                            // Extra cycle when data comes from an E or M state.
                            if (otherState == EXCLUSIVE || otherState == MODIFIED)
                                transferCycles += 1;
                            
                            core.extime += transferCycles + 1; // +1 for execution after transfer.
                            globalCycle += transferCycles + 1;
                            
                            // After transfer, mark line in L1 as SHARED.
                            core.cache[address] = SHARED;
                            
                            // For a write–back may need to update bus traffic and invalidations.
                            totalBusTraffic += blockSize;
                        } else {
                            // Data not found in any other cache: fetch from memory.
                            int memAccessCycles = 100;
                            core.extime += memAccessCycles + 1; // +1 for execute cycle.
                            globalCycle += memAccessCycles + 1;
                            
                            // Set state to EXCLUSIVE.
                            core.cache[address] = EXCLUSIVE;
                        }
                    }
                }
                // Process write instruction.
                else if (op == 'W') {
                    // Write Hit.
                    if (core.cache.find(address) != core.cache.end() &&
                        core.cache[address] != INVALID) {
                        // If state is SHARED, send invalidations to other cores.
                        if (core.cache[address] == SHARED) {
                            for (int j = 0; j < numCores; j++) {
                                if (j == coreId) continue;
                                if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                    cores[j].cache[address] != INVALID) {
                                    // Invalidate other core's copy.
                                    cores[j].cache[address] = INVALID;
                                    totalInvalidations++;
                                    totalBusTraffic += blockSize;
                                }
                            }
                            // Then update own state to MODIFIED.
                            core.cache[address] = MODIFIED;
                            core.extime += 1;
                            globalCycle += 1;
                        } else if (core.cache[address] == EXCLUSIVE || core.cache[address] == MODIFIED) {
                            // Write hit in EXCLUSIVE or MODIFIED state takes 1 cycle.
                            core.extime += 1;
                            globalCycle += 1;
                            core.cache[address] = MODIFIED;
                        }
                    } else {
                        // Write Miss.
                        // Invalidate copies from other caches, if any.
                        for (int j = 0; j < numCores; j++) {
                            if (j == coreId) continue;
                            if (cores[j].cache.find(address) != cores[j].cache.end() &&
                                cores[j].cache[address] != INVALID) {
                                cores[j].cache[address] = INVALID;
                                totalInvalidations++;
                                totalBusTraffic += blockSize;
                            }
                        }
                        // Fetch data from memory.
                        int memAccessCycles = 100;
                        core.extime += memAccessCycles + 1;
                        globalCycle += memAccessCycles + 1;
                        
                        // After write miss, update own state to MODIFIED.
                        core.cache[address] = MODIFIED;
                    }
                }
                
                // Mark bus as free.
                busFree = true;
                busOwner = -1;
                executed = true;
                
                // Read next line for this core.
                if (!std::getline(core.trace, core.currentLine))
                    core.finished = true;
            }
        }
        
        // If no core executed (e.g. all waiting for the bus), advance the clock.
        if (!executed) {
            globalCycle++;
            for (int coreId = 0; coreId < numCores; coreId++) {
                if (!cores[coreId].finished)
                    cores[coreId].idletime++;
            }
        }
    }
    
    printStatistics();
}

//
// Print simulation statistics.
//
void CacheSimulator::printStatistics() {
    std::ofstream outFile;
    if (!outFileName.empty()) {
        outFile.open(outFileName);
    }
    
    std::ostream &out = (outFile.is_open() ? outFile : std::cout);
    out << "Flat Cache Simulation Results:" << std::endl
        << "Global Clock Cycles: " << globalCycle << std::endl << std::endl;
    
    for (int i = 0; i < numCores; i++) {
        out << "Core " << i << " Statistics:" << std::endl;
        out << "  Total execution time: " << cores[i].extime << " cycles" << std::endl;
        out << "  Total idle time: " << cores[i].idletime << " cycles" << std::endl;
        out << std::endl;
    }
    
    out << "Global Statistics:" << std::endl;
    out << "  Total invalidations: " << totalInvalidations << std::endl;
    out << "  Total bus traffic: " << totalBusTraffic << " bytes" << std::endl;
    
    if (outFile.is_open())
        outFile.close();
}