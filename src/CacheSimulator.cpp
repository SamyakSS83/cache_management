#include "CacheSimulator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

CacheSimulator::CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                              const std::string& outFileName) {
    this->outFileName = outFileName;
    // this->s = s;
    // this->E = E;
    // this->b = b;
    // this->traceFilePrefix = traceFilePrefix;
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
    std::vector<bool> coreFinished(numCores, false);
    std::vector<std::string> currentLines(numCores);
    std::vector<bool> coreBlocked(numCores, false);
    std::vector<int> coreCycles(numCores, 0);
    std::vector<int> busRequestCycle(numCores, 0); // Track when each core requested the bus
    stalledSince.resize(numCores, 0); // Initialize tracking of stall beginning
    
    // Read first line from each trace file
    for (int i = 0; i < numCores; i++) {
        if (std::getline(traceFiles[i], currentLines[i])) {
            // Core has a line to process
        } else {
            coreFinished[i] = true;
        }
    }
    
    // Main simulation loop
    while (!std::all_of(coreFinished.begin(), coreFinished.end(), [](bool v) { return v; })) {
        globalCycle++;
        
        // Process bus requests in fair order (FIFO with index priority)
        if (!busLocked) {
            // Find the core with the oldest pending bus request
            // If tie, prefer the core with lower index
            int selectedCore = -1;
            int oldestRequestCycle = globalCycle + 1;
            
            for (int i = 0; i < numCores; i++) {
                if (!coreFinished[i] && !coreBlocked[i]) {
                    if (busRequestCycle[i] < oldestRequestCycle) {
                        // This core has an older request
                        oldestRequestCycle = busRequestCycle[i];
                        selectedCore = i;
                    }
                    else if (busRequestCycle[i] == oldestRequestCycle) {
                        // Same request cycle - lower index gets priority
                        // selectedCore will already be the lower index core if set
                        if (selectedCore == -1 || i < selectedCore) {
                            selectedCore = i;
                        }
                    }
                }
            }
            
            if (selectedCore >= 0) {
                busLocked = true;
                busOwner = selectedCore;
                
                // If this core was stalled, calculate idle time
                if (stalledSince[selectedCore] > 0) {
                    int idleTime = globalCycle - stalledSince[selectedCore];
                    caches[selectedCore]->addIdleTime(idleTime);
                    stalledSince[selectedCore] = 0; // Reset stall tracking
                }
                
                // Parse the current line for the bus owner
                std::istringstream iss(currentLines[busOwner]);
                char op;
                std::string addressStr;
                
                iss >> op >> addressStr;
                
                // Convert hex address to integer
                unsigned int address = std::stoul(addressStr, nullptr, 16);
                
                // Process the memory operation
                MemoryOperation memOp = (op == 'R') ? READ : WRITE;
                std::vector<Cache*> otherCaches;
                for (int j = 0; j < numCores; j++) {
                    if (j != busOwner) otherCaches.push_back(caches[j]);
                }
                
                int currentCycle = coreCycles[busOwner];
                
                // Initialize isHit before the debug block
                bool isHit;
                
                // Debug output if enabled
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
                    
                    // Process the request
                    isHit = caches[busOwner]->processRequest(memOp, address, currentCycle, otherCaches);
                    
                    // Get the new state after processing
                    lineIndex = caches[busOwner]->findLineInSetPublic(setIndex, tag);
                    CacheLineState newState = INVALID;
                    
                    if (lineIndex != -1) {
                        newState = caches[busOwner]->getLineState(setIndex, lineIndex);
                    }
                    
                    // Print focused debug info about this instruction
                    caches[busOwner]->printDebugInfo(memOp, address, isHit, oldState, newState);
                    
                    // Show bus activity if relevant
                    if (memOp == WRITE && oldState == SHARED) {
                        std::cout << "  → Bus: Sending invalidation to other caches" << std::endl;
                    } else if (!isHit && (oldState == MODIFIED || oldState == EXCLUSIVE)) {
                        std::cout << "  → Bus: Cache-to-cache transfer" << std::endl;
                    } else if (!isHit) {
                        std::cout << "  → Bus: Memory access" << std::endl;
                    }
                    
                    std::cout << "================================" << std::endl << std::endl;
                }
                else {
                    // Just process without debug output
                    isHit = caches[busOwner]->processRequest(memOp, address, currentCycle, otherCaches);
                }
                
                // If it's a miss, block the core until the specific time
                if (!isHit) {
                    coreBlocked[busOwner] = true;
                } else {
                    // Process next line immediately for this core if it's a hit
                    if (std::getline(traceFiles[busOwner], currentLines[busOwner])) {
                        // Core has another line to process
                        busRequestCycle[busOwner] = globalCycle; // Update request time
                    } else {
                        coreFinished[busOwner] = true;
                    }
                }
                
                busLocked = false;
                busOwner = -1;
            }
        } else {
            // Bus is locked, mark cores as stalled if they need the bus
            for (int i = 0; i < numCores; i++) {
                if (!coreFinished[i] && !coreBlocked[i] && stalledSince[i] == 0) {
                    stalledSince[i] = globalCycle; // Mark when stalling began
                }
            }
        }
        
        // Check if any blocked cores can be unblocked
        for (int i = 0; i < numCores; i++) {
            if (coreBlocked[i] && coreCycles[i] <= globalCycle) {
                coreBlocked[i] = false;
                
                // When unblocked, read next line
                if (std::getline(traceFiles[i], currentLines[i])) {
                    // Core has another line to process
                    busRequestCycle[i] = globalCycle; // Update request time
                } else {
                    coreFinished[i] = true;
                }
            }
        }
        
        // For cores that aren't finished or blocked and haven't been processed yet,
        // record their bus request
        for (int i = 0; i < numCores; i++) {
            if (!coreFinished[i] && !coreBlocked[i] && busRequestCycle[i] == 0) {
                busRequestCycle[i] = globalCycle;
            }
        }
    }
    
    // Collect final statistics
    for (int i = 0; i < numCores; i++) {
        totalInvalidations += caches[i]->getBusInvalidations();
        totalBusTraffic += caches[i]->getBusTraffic();
    }
    
    // Print statistics
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
