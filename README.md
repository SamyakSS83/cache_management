# Cache Management Simulation System

A multi-core cache simulator that implements the MESI (Modified, Exclusive, Shared, Invalid) cache coherence protocol. This simulator models a quad-core system with individual L1 caches connected via a central snooping bus.

## Features
- Simulation of a 4-core system with separate L1 caches
- Complete MESI protocol implementation
- LRU (Least Recently Used) replacement policy
- Write-back/Write-allocate cache policy
- Bus traffic and coherence tracking
- Detailed statistics for each core and overall system performance

## Building the Project

### Prerequisites
- C++ compiler supporting C++11 or later (GCC or Clang recommended)
- Make 

### Build Instructions
1. Navigate to the project directory
2. run `make`

## Running the Simulator

### Command Line Arguments
```
Where:
-t <tracefile>: Name of parallel application (e.g. app1) whose 4 traces are to be used
-s <s>: Number of set index bits (number of sets in the cache = S = 2^s)
-E <E>: Associativity (number of cache lines per set)
-b <b>: Number of block bits (block size = B = 2^b bytes)
-o <outfilename>: Optional. Logs output in file for plotting etc.
-d: Optional. Enable debug mode (prints cache state after each instruction)
-h: Optional. Prints help information
```

### Trace File Format
The simulator expects four trace files with names matching the pattern `<tracefile>_proc<N>.trace`, where `<N>` is 0, 1, 2, or 3 for each core. Each line in a trace file represents a memory operation.

## Output Format
The simulator outputs statistics about the simulation including:

### Simulation parameters
- Per-core statistics:
    - Total instructions executed
    - Read/write counts
    - Execution and idle cycles
    - Cache misses and miss rate
    - Evictions and writebacks
    - Bus invalidations
    - Data traffic
- Overall bus summary:
    - Total bus transactions
    - Total bus traffic in bytes

## Notes on Cache Coherence
The simulator implements the MESI protocol:

- M (Modified): Line is modified and only in this cache
- E (Exclusive): Line is unmodified and only in this cache
- S (Shared): Line may be in other caches and is unmodified
- I (Invalid): Line is invalid

When a read or write miss occurs, the simulator checks for copies in other caches and handles coherence appropriately, updating the MESI states and tracking bus traffic.
