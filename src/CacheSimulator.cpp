#include "CacheSimulator.h"
#include <bits/stdc++.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

CacheSimulator::CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                              const std::string& outFileName)
  : globalCycle(0), busLocked(false), busOwner(-1), busFreeCycle(0) {
    this->outFileName = outFileName;
    numCores = 4; // Quad-core
    totalInvalidations = 0;
    totalBusTraffic = 0;
    debugMode = false;
    
    // Initialize caches for each core
    for (int i = 0; i < numCores; i++) {
        caches.push_back(new Cache(i, s, E, b));
    }
    
    // Open trace files
    for (int i = 0; i < numCores; i++) {
        std::string fileName = traceFilePrefix + "_proc" + std::to_string(i) + ".trace";
        traceFiles.push_back(std::ifstream(fileName));
        
        if (!traceFiles[i].is_open()) {
            std::cerr << "Error opening trace file: " << fileName << std::endl;
            exit(1);
        }
    }
}

CacheSimulator::~CacheSimulator() {
    // Close trace files
    for (auto& file : traceFiles) {
        if (file.is_open()) {
            file.close();
        }
    }
    
    // Free cache memory
    for (auto cache : caches) {
        delete cache;
    }
}

void CacheSimulator::runSimulation() {
    int numCores = caches.size();
    std::vector<bool> coreFinished(numCores, false);
    std::vector<std::string> currentLines(numCores);
    std::vector<bool> coreBlocked(numCores, false);
    std::vector<int> totTime(numCores, 0);
    std::vector<int> extTime(numCores, 0);
    std::vector<int> idleTime(numCores, 0);
    stalledSince.resize(numCores, 0);
    std::vector<int> unblockTime(numCores, 0);
    std::vector<int> requestTime(numCores, 0);
    
    for (int i = 0; i < numCores; i++) {
        if (std::getline(traceFiles[i], currentLines[i])) {
            requestTime[i] = globalCycle;
        } else {
            coreFinished[i] = true;
        }
    }
    
    while (!std::all_of(coreFinished.begin(), coreFinished.end(), [](bool v){return v;})) {
        globalCycle++;

        if (debugMode) {
            std::cout << "\n=== Global Cycle " << globalCycle
                      << " | BusLocked=" << busLocked
                      << " | BusFreeAt=" << busFreeCycle
                      << " | BusOwner=" << busOwner << " ===\n";
        }

        // 1) unlock bus if its transaction has finished
        if (busLocked && globalCycle >= busFreeCycle) {
            busLocked = false;
            int prevOwner = busOwner;
            busOwner = -1;
            if (debugMode) {
                std::cout << "[Cycle " << globalCycle
                          << "] BUS UNLOCKED (was held by Core " << prevOwner << ")\n";
            }
        }

        // 2) every core that is free (not finished, not blocked) and not 
        //    itself using the bus, accumulates 1 cycle idle
        for (int i = 0; i < numCores; i++) {
            if (!coreFinished[i] && !coreBlocked[i] && ! (busLocked && i==busOwner))
                idleTime[i]++;
        }

        // 3) if bus is free, start next FIFO request
        if (!busLocked) {
            int selectedCore = -1, oldest = INT_MAX;
            for (int i = 0; i < numCores; i++) {
                if (!coreFinished[i] && !coreBlocked[i] && requestTime[i] < oldest) {
                    oldest = requestTime[i];
                    selectedCore = i;
                }
            }
            if (selectedCore >= 0) {
                // debug: announce grant
                if (debugMode) {
                    std::cout << "[Cycle " << globalCycle
                              << "] Core " << selectedCore
                              << " granted BUS (requested at cycle " << requestTime[selectedCore]
                              << ")\n";
                }

                // parse trace line
                busLocked = true;
                busOwner  = selectedCore;
                std::istringstream iss(currentLines[busOwner]);
                char op;
                std::string addr;
                iss >> op >> addr;
                unsigned address = std::stoul(addr, nullptr, 16);
                MemoryOperation memOp = (op == 'R' ? READ : WRITE);
                std::vector<Cache*> others;
                for (int j = 0; j < numCores; j++)
                    if (j != busOwner) others.push_back(caches[j]);

                // process request
                auto res = caches[busOwner]->processRequest(memOp, address, others);
                bool isHit = res.isHit;
                int busTime = std::max(0, res.execTime - 1);

                // schedule bus unlock
                busFreeCycle = globalCycle + busTime;

                if (debugMode) {
                    std::cout << "[Cycle " << globalCycle
                              << "] BUS will be held for " << busTime
                              << " cycles, free at cycle " << busFreeCycle << "\n";
                }

                // block core until bus+core complete
                coreBlocked[busOwner] = true;
                unblockTime[busOwner] = globalCycle + res.execTime;

                // record its 1 cycle of work
                extTime[busOwner] += 1;
                totTime[busOwner] = extTime[busOwner] + idleTime[busOwner];

                // debug verbosity
                if (debugMode) {
                    std::cout << "========== Cycle " << globalCycle << " ==========\n";
                    // Get the cache line's current state before processing (if it exists)
                    unsigned int setIndex = caches[busOwner]->getSetIndexPublic(address);
                    unsigned int tag = caches[busOwner]->getTagPublic(address);
                    int lineIndex = caches[busOwner]->findLineInSetPublic(setIndex, tag);
                    CacheLineState oldState = INVALID;
                    if (lineIndex != -1 && caches[busOwner]->getLineState(setIndex, lineIndex) != INVALID) {
                        oldState = caches[busOwner]->getLineState(setIndex, lineIndex);
                    }
                    CacheLineState newState = (lineIndex != -1 ? caches[busOwner]->getLineState(setIndex, lineIndex) : INVALID);

                    // Print focused debug info about the instruction.
                    caches[busOwner]->printDebugInfo(memOp, address, isHit, oldState, newState);
                    
                    if (memOp == WRITE && oldState == SHARED) {
                        std::cout << "  → Bus: Sending invalidation to other caches" << std::endl;
                    } else if (!isHit && (oldState == MODIFIED || oldState == EXCLUSIVE)) {
                        std::cout << "  → Bus: Cache-to-cache transfer" << std::endl;
                    } else if (!isHit) {
                        std::cout << "  → Bus: Memory access" << std::endl;
                    }
                    
                    std::cout << "================================" << std::endl << std::endl;
                } else {
                    // only on hit do we immediately fetch next instruction
                    if (isHit) {
                        if (std::getline(traceFiles[busOwner], currentLines[busOwner]))
                            requestTime[busOwner] = globalCycle;
                        else
                            coreFinished[busOwner] = true;
                    }
                }
            }
        }

        // 4) unblock cores whose whole exec (bus+core) is done
        for (int i = 0; i < numCores; i++) {
            if (coreBlocked[i] && globalCycle >= unblockTime[i]) {
                coreBlocked[i] = false;
                if (std::getline(traceFiles[i], currentLines[i]))
                    requestTime[i] = globalCycle;
                else coreFinished[i] = true;
            }
        }
    }

    for (int i = 0; i < numCores; i++) {
        caches[i]->setTotalCycles(totTime[i]);
        caches[i]->setIdleCycles(idleTime[i]);
    }
    
    printStatistics();
}

void CacheSimulator::printStatistics() {
    std::ofstream outFile;
    if (!outFileName.empty()) {
        outFile.open(outFileName);
    }
    
    std::ostream& out = outFileName.empty() ? std::cout : outFile;
    
    out << "Cache Simulation Results:" << std::endl;
    out << "=========================" << std::endl << std::endl;
    
    // Print per-core statistics
    for (int i = 0; i < numCores; i++) {
        out << "Core " << i << " Statistics:" << std::endl;
        out << "  Read instructions: " << caches[i]->getReadCount() << std::endl;
        out << "  Write instructions: " << caches[i]->getWriteCount() << std::endl;
        out << "  Total memory references: " << caches[i]->getReadCount() + caches[i]->getWriteCount() << std::endl;
        out << "  Cache misses: " << caches[i]->getMissCount() << std::endl;
        out << "  Cache hits: " << caches[i]->getHitCount() << std::endl;
        out << "  Miss rate: " << std::fixed << std::setprecision(6) << caches[i]->getMissRate() * 100 << "%" << std::endl;
        out << "  Cache evictions: " << caches[i]->getEvictionCount() << std::endl;
        out << "  Cache writebacks: " << caches[i]->getWritebackCount() << std::endl;
        out << "  Total execution cycles: " << caches[i]->getTotalCycles() << std::endl;
        out << "  Idle cycles: " << caches[i]->getIdleCycles() << std::endl;
        out << std::endl;
    }
    
    // Print global statistics
    out << "Global Statistics:" << std::endl;
    out << "  Total invalidations on the bus: " << totalInvalidations << std::endl;
    out << "  Total data traffic on the bus: " << totalBusTraffic << " bytes" << std::endl;
    out << std::endl;
    
    if (outFile.is_open()) {
        outFile.close();
    }
}
