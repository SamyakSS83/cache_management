#ifndef CACHE_SIMULATOR_H
#define CACHE_SIMULATOR_H

#include <string>
#include <vector>
#include <fstream>
#include <utility>
#include <bits/stdc++.h>


class CacheSimulator {
private:
    std::vector<struct CoreState> cores; // now holds per-core simulation state
    std::string outFileName;
    int numCores;
    int totalInvalidations;
    int totalBusTraffic; // in bytes
    int globalCycle;
    bool busFree;
    int busOwner;
    int blockSize;     // Derived from block bits b: blockSize = 2^b

public:
    CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                   const std::string& outFileName);
    ~CacheSimulator();
    void runSimulation();
    void printStatistics();
};

#endif // CACHE_SIMULATOR_H
