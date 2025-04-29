#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

// Memory operation types
enum MemoryOperation {
    READ,
    WRITE
};

// MESI cache coherence protocol states
enum CacheLineState {
    INVALID,
    SHARED,
    EXCLUSIVE,
    
    MODIFIED
};

// String representation of cache line states
inline std::string stateToString(CacheLineState state) {
    switch (state) {
        case INVALID: return "I";
        case SHARED: return "S";
        case EXCLUSIVE: return "E";
        case MODIFIED: return "M";
        default: return "Unknown";
    }
}

// Bus transaction types
// enum BusTransaction {
//     BUS_READ,
//     BUS_WRITE,
//     BUS_UPGRADE,
//     BUS_INVALIDATE
// };

#endif // UTILS_H
