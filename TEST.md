# Cache Coherence Test Cases

This document describes several **test cases** designed to evaluate different aspects of the **MESI protocol** and memory sharing behaviors.

## Test Cases Overview

### 1. `app4`: True Coherence Sharing
- **Demonstrates MESI protocol in action.**
- Shows **true coherence sharing** between `proc0` and `proc1`.

### 2. `app5`: False Sharing Between Two Processors
- Tests **false sharing** effects between `proc0` and `proc1`.
- Useful for understanding unnecessary cache invalidations due to sharing of cache lines.

### 3. `app6`: False Sharing Across Multiple Processors
- Expands **false sharing** demonstration to a larger **multi-processor scenario**.
- Helps analyze performance impact on broader workloads.

### 4. `app7`: Write-Back Verification
- **Verifies correct write-back behavior** in the MESI protocol.
- Ensures that when a processor is in the **M (Modified) state**, and receives a **RWITM (Read-With-Intent-to-Modify) bus request**, the correct sequence occurs.

### 5. `app8`: Stress Testing Multiple Writes
- Simulates **multiple processors writing to the same memory location**.
- Useful for testing write contention scenarios.

### 6. `app9`: Another False Sharing Test
- Similar to `app6`, tests **false sharing effects** in a varied execution pattern.

## Running a Test Case

To execute a test case, use the following commands:

```sh
make
./L1simulate -t assignment3_traces/app{x} -s 6 -b 5 -E 2 [-d]
```