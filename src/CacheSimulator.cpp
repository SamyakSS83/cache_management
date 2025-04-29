#include "CacheSimulator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

CacheSimulator::CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                              const std::string& outFileName) {
    this->outFileName = outFileName;
    numCores = 4; // Quad-core
    totalInvalidations = 0;
    totalBusTraffic = 0;
    globalCycle = 0;
    busLocked = false;
    busOwner = -1;
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
    // Instead of using a separate cycle counter per core,
    // we track totaltime, extime and idletime for each core.
    std::vector<int> totTime(numCores, 0);
    std::vector<int> extTime(numCores, 0);
    std::vector<int> idleTime(numCores, 0);
    // When cores are blocked (e.g. waiting for bus), record when stall began.
    stalledSince.resize(numCores, 0);
    // For a blocked core, record when it can become unblocked.
    std::vector<int> unblockTime(numCores, 0);
    // Track request start time per core (used for fairness)
    std::vector<int> requestTime(numCores, 0);
    
    // Read the first line from each trace file.
    for (int i = 0; i < numCores; i++) {
        if (std::getline(traceFiles[i], currentLines[i])) {
            requestTime[i] = globalCycle;
        } else {
            coreFinished[i] = true;
        }
    }
    
    while (!std::all_of(coreFinished.begin(), coreFinished.end(), [](bool v) { return v; })) {
        globalCycle++;
        
        // For every core not processing an instruction this cycle and not finished,
        // add 1 cycle to its idle time.
        for (int i = 0; i < numCores; i++) {
            if (!coreFinished[i] && !coreBlocked[i])
                idleTime[i]++;
        }
        
        // Process bus requests in a fair (FIFO) order.
        if (!busLocked) {
            int selectedCore = -1;
            int oldestRequest = globalCycle + 1;
            for (int i = 0; i < numCores; i++) {
                if (!coreFinished[i] && !coreBlocked[i]) {
                    if (requestTime[i] < oldestRequest) {
                        oldestRequest = requestTime[i];
                        selectedCore = i;
                    }
                }
            }
            
            if (selectedCore >= 0) {
                busLocked = true;
                busOwner = selectedCore;
                // If the core was stalled, add that waiting time to idleTime.
                if (stalledSince[selectedCore] > 0) {
                    idleTime[selectedCore] += (globalCycle - stalledSince[selectedCore]);
                    stalledSince[selectedCore] = 0;
                }
                
                std::istringstream iss(currentLines[busOwner]);
                char op;
                std::string addressStr;
                iss >> op >> addressStr;
                unsigned int address = std::stoul(addressStr, nullptr, 16);
                MemoryOperation memOp = (op == 'R') ? READ : WRITE;
                std::vector<Cache*> otherCaches;
                for (int j = 0; j < numCores; j++) {
                    if (j != busOwner)
                        otherCaches.push_back(caches[j]);
                }
                
                // Process the memory request (using the updated processRequest signature)
                auto result = caches[busOwner]->processRequest(memOp, address, otherCaches);
                // Update core active execution time and totaltime.
                extTime[busOwner] += result.execTime;
                totTime[busOwner] = extTime[busOwner] + idleTime[busOwner];
                bool isHit = result.isHit;
                
                if (debugMode) {
                    std::cout << "========== Cycle " << globalCycle << " ==========" << std::endl;
                    
                    // Get the cache line's current state before processing (if it exists)
                    unsigned int setIndex = caches[busOwner]->getSetIndexPublic(address);
                    unsigned int tag = caches[busOwner]->getTagPublic(address);
                    int lineIndex = caches[busOwner]->findLineInSetPublic(setIndex, tag);
                    CacheLineState oldState = INVALID;
                    if (lineIndex != -1 && caches[busOwner]->getLineState(setIndex, lineIndex) != INVALID) {
                        oldState = caches[busOwner]->getLineState(setIndex, lineIndex);
                    }
                    
                    // Print focused debug info about the instruction.
                    // (Assume printDebugInfo uses the latest cache state.)
                    caches[busOwner]->printDebugInfo(memOp, address, isHit, oldState, 
                                                      (lineIndex != -1 ? caches[busOwner]->getLineState(setIndex, lineIndex) : INVALID));
                    
                    if (memOp == WRITE && oldState == SHARED) {
                        std::cout << "  → Bus: Sending invalidation to other caches" << std::endl;
                    } else if (!isHit && (oldState == MODIFIED || oldState == EXCLUSIVE)) {
                        std::cout << "  → Bus: Cache-to-cache transfer" << std::endl;
                    } else if (!isHit) {
                        std::cout << "  → Bus: Memory access" << std::endl;
                    }
                    
                    std::cout << "================================" << std::endl << std::endl;
                    // In debug mode do not block the core.
                } else {
                    // For a hit, immediately fetch next instruction.
                    if (std::getline(traceFiles[busOwner], currentLines[busOwner])) {
                        requestTime[busOwner] = globalCycle;
                    } else {
                        coreFinished[busOwner] = true;
                    }
                }
                
                // For a miss, block the core until its active execution completes.
                if (!isHit) {
                    if (memOp == READ) {
                        // For read misses, add extra idle time of 100 cycles.
                        idleTime[busOwner] += 100;
                        totTime[busOwner] = extTime[busOwner] + idleTime[busOwner];
                        // The core remains blocked for (100 + result.execTime) cycles.
                        unblockTime[busOwner] = globalCycle + 100 + result.execTime;
                    } else {
                        // For write misses, use the returned execTime.
                        unblockTime[busOwner] = globalCycle + result.execTime;
                    }
                    coreBlocked[busOwner] = true;
                }
                
                busLocked = false;
                busOwner = -1;
            }
        }
        
        // Unblock cores whose waiting (for bus/memory) is complete.
        for (int i = 0; i < numCores; i++) {
            if (coreBlocked[i] && globalCycle >= unblockTime[i]) {
                coreBlocked[i] = false;
                // Read the next instruction (if any).
                if (std::getline(traceFiles[i], currentLines[i])) {
                    requestTime[i] = globalCycle;
                } else {
                    coreFinished[i] = true;
                }
            }
        }
    }
    
    // After simulation, update each cache's total cycle counter.
    for (int i = 0; i < numCores; i++) {
        // Each cache now records totTime from simulation.
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
