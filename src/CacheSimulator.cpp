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
        
        // Process one instruction from each core
        for (int i = 0; i < numCores; i++) {
            if (coreFinished[i] || coreBlocked[i]) continue;
            
            // Parse the current line
            std::istringstream iss(currentLines[i]);
            char op;
            std::string addressStr;
            
            iss >> op >> addressStr;
            
            // Convert hex address to integer
            unsigned int address = std::stoul(addressStr, nullptr, 16);
            
            // Process the memory operation
            MemoryOperation memOp = (op == 'R') ? READ : WRITE;
            std::vector<Cache*> otherCaches;
            for (int j = 0; j < numCores; j++) {
                if (j != i) otherCaches.push_back(caches[j]);
            }
            
            int currentCycle = coreCycles[i];
            bool isHit = caches[i]->processRequest(memOp, address, currentCycle, otherCaches);
            
            // Update core cycle count
            coreCycles[i] = currentCycle;
            
            // If it's a miss, block the core until the miss is resolved
            if (!isHit) {
                coreBlocked[i] = true;
            }
            
            // Read next line for this core
            if (!coreBlocked[i]) {
                if (std::getline(traceFiles[i], currentLines[i])) {
                    // Core has another line to process
                } else {
                    coreFinished[i] = true;
                }
            }
        }
        
        // Check if any blocked cores can be unblocked
        for (int i = 0; i < numCores; i++) {
            if (coreBlocked[i] && coreCycles[i] <= globalCycle) {
                coreBlocked[i] = false;
                
                // Read next line for this core
                if (std::getline(traceFiles[i], currentLines[i])) {
                    // Core has another line to process
                } else {
                    coreFinished[i] = true;
                }
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
