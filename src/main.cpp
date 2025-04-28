#include "CacheSimulator.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <getopt.h>

void printHelp() {
    std::cout << "Usage: ./L1simulate -t <tracefile> -s <s> -E <E> -b <b> [-o <outfilename>] [-d] [-h]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -t <tracefile>: name of parallel application (e.g. app1) whose 4 traces are to be used" << std::endl;
    std::cout << "  -s <s>: number of set index bits (number of sets in the cache = S = 2^s)" << std::endl;
    std::cout << "  -E <E>: associativity (number of cache lines per set)" << std::endl;
    std::cout << "  -b <b>: number of block bits (block size = B = 2^b)" << std::endl;
    std::cout << "  -o <outfilename>: logs output in file for plotting etc." << std::endl;
    std::cout << "  -d: enable debug mode (prints cache state after each instruction)" << std::endl;
    std::cout << "  -h: prints this help" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string traceFile;
    int s = 0, E = 0, b = 0;
    std::string outFileName;
    bool debugMode = false;
    
    // Parse command line arguments
    int opt;
    while ((opt = getopt(argc, argv, "t:s:E:b:o:dh")) != -1) {
        switch (opt) {
            case 't':
                traceFile = optarg;
                break;
            case 's':
                s = std::stoi(optarg);
                break;
            case 'E':
                E = std::stoi(optarg);
                break;
            case 'b':
                b = std::stoi(optarg);
                break;
            case 'o':
                outFileName = optarg;
                break;
            case 'd':
                debugMode = true;
                break;
            case 'h':
                printHelp();
                return 0;
            default:
                printHelp();
                return 1;
        }
    }
    
    // Validate parameters
    if (traceFile.empty()) {
        std::cerr << "Error: Missing trace file prefix (-t)" << std::endl;
        printHelp();
        return 1;
    }
    
    if (s <= 0) {
        std::cerr << "Error: Invalid set index bits (-s)" << std::endl;
        return 1;
    }
    
    if (E <= 0) {
        std::cerr << "Error: Invalid associativity (-E)" << std::endl;
        return 1;
    }
    
    if (b <= 0) {
        std::cerr << "Error: Invalid block bits (-b)" << std::endl;
        return 1;
    }
    
    // Create and run the simulator
    try {
        CacheSimulator simulator(traceFile, s, E, b, outFileName, debugMode);
        simulator.runSimulation();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
