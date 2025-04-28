#ifndef CACHE_SIMULATOR_H
#define CACHE_SIMULATOR_H

#include <string>
#include <vector>
#include <fstream>
#include <utility>

class CacheSimulator {
private:
    std::vector<struct CoreState> cores; // now holds per-core simulation state
    std::string outFileName;
    int numCores;
    int totalInvalidations;
    int totalBusTraffic; // in bytes
    int totalBusTransactions;
    int globalCycle;
    bool busFree;
    int busOwner;
    int blockSize;     // Derived from block bits b: blockSize = 2^b
    bool debugMode;    // Flag for debug output
    
    // Cache configuration
    int setIndexBits;  // s
    int associativity; // E
    int blockBits;     // b
    int numSets;       // 2^s

public:
    CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                   const std::string& outFileName, bool debug = false);
    ~CacheSimulator();
    void runSimulation();
    void printStatistics();
    void debugPrint(const std::string& message);
};

#endif // CACHE_SIMULATOR_H