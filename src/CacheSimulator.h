#ifndef CACHE_SIMULATOR_H
#define CACHE_SIMULATOR_H

#include "Cache.h"
#include <string>
#include <vector>
#include <fstream>

class CacheSimulator {
private:
    std::vector<Cache*> caches;
    std::vector<std::ifstream> traceFiles;
    std::string outFileName;
    int numCores;
    int totalInvalidations;
    int totalBusTraffic; // in bytes
    int globalCycle;
    bool busLocked;
    int busOwner; // Core ID of the bus owner
    std::vector<int> stalledSince; // Tracks when cores began waiting for the bus
    bool debugMode;

public:
    CacheSimulator(const std::string& traceFilePrefix, int s, int E, int b, 
                  const std::string& outFileName);
    ~CacheSimulator();
    void runSimulation();
    void printStatistics();
    void setDebugMode(bool enable) { debugMode = enable; }
};

#endif // CACHE_SIMULATOR_H
