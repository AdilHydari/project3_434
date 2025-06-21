# ECE 434 Project 3: Thread Teams with Signal Handling

## Overview
This project implements a multi-threaded program with 4 teams of pthread threads. Each team handles specific signals and sorts a portion of a large integer array using sequential quicksort.

## Architecture Overview

### Core Design
- **Multi-threaded**: 4 teams of pthread threads, each team sorts a portion of a large integer array
- **Signal Handling**: Each team handles 3 specific signals using custom signal handlers with `sigaction()`
- **Sorting Strategy**: Sequential quicksort (Case 1) - only the first thread in each team performs sorting
- **Thread Management**: Completion tracking with mutex protection, clean resource cleanup

### Signal Assignment by Team
- **Team 0**: SIGINT, SIGABRT, SIGILL
- **Team 1**: SIGCHLD, SIGSEGV, SIGFPE  
- **Team 2**: SIGHUP, SIGTSTP, SIGINT
- **Team 3**: SIGABRT, SIGFPE, SIGHUP

### Key Implementation Details
- Uses `pthread_sigmask()` to block signals not assigned to each team
- Signal handlers print detailed logging with timestamps and thread identification
- Performance timing with `clock_gettime(CLOCK_MONOTONIC)`
- Memory allocation for team subarrays with proper cleanup
- Two program versions: `project1.c` (main) and `project1_signals.c` (enhanced signal testing)

## Building the Project

```bash
# Build all targets
make all

# Build specific targets
make project1           # Main implementation
make project1_signals   # Signal testing version
make signal_tester      # Signal utility

# Clean build files
make clean

# Test commands
make test_quick         # Quick test (1,000 elements)
make test_signals       # Signal testing version
make signal_test        # Automated signal tests using script
```

## Program Execution

### Main Program
```bash
# Default: 10,000 elements, 4 threads per team
./project1

# Custom parameters
./project1 <array_size> <threads_per_team>
./project1 100000 100   # Large test case
```

### Signal Testing
```bash
# Manual signal testing
./project1_signals <array_size> <threads_per_team> <signal_test_mode>
./project1_signals 50000 10 1 &
echo $!  # Note the PID

# Send signals using utility
./signal_tester <pid> <signal_number>
./signal_tester 1234 2  # Send SIGINT

# Automated testing
./simple_signal_test.sh          # Interactive signal testing script
./better_test.sh                 # Enhanced test suite with logging
./better_test.sh quick           # Quick command-line test
./better_test.sh signals         # Comprehensive signal tests
./better_test.sh perf           # Performance analysis
```

## Testing Approach
The project includes comprehensive signal testing capabilities:
- Interactive signal testing mode
- Automated signal delivery testing
- Performance comparison between versions
- Verification of signal masking and handler assignment

### Manual Signal Testing
```bash
# Step 1: Run the program in background
./project1 50000 10 &
echo $!  # Note the process ID

# Step 2: Send signals using the tester
./signal_tester <pid> 2   # SIGINT
./signal_tester <pid> 6   # SIGABRT
./signal_tester <pid> 1   # SIGHUP

# Or use kill command
kill -INT <pid>   # SIGINT
kill -ABRT <pid>  # SIGABRT
kill -HUP <pid>   # SIGHUP
```

## Expected Output

The program will display:
1. Initialization messages showing team creation
2. Signal handling messages when signals are caught
3. Sorting progress and completion times
4. Final completion order of teams

## Design Decisions

### Signal Handling Strategy
- Used `sigaction()` for portable signal handling
- Each team blocks signals not assigned to them using `pthread_sigmask()`
- Signal handlers print detailed team ID and thread ID for identification

### Sorting Implementation  
- **Case 1**: Sequential quicksort implementation
- Only the first thread in each team performs sorting (representative thread)
- Other threads wait for completion
- Timing measurements using `clock_gettime(CLOCK_MONOTONIC)`

### Thread Management
- Teams are independent pthread groups
- Completion tracking with mutex protection
- Clean thread joining and resource cleanup

## Known Considerations

### Signal Delivery
1. **Process vs Thread Signals**: Some signals (like SIGINT from Ctrl+C) go to the entire process
2. **Dangerous Signals**: SIGSEGV and SIGFPE can cause immediate termination
3. **Signal Masking**: Complex interaction between pthread signal masks with overlapping team assignments

### Performance
1. **Thread Overhead**: Many threads may not improve performance for small arrays
2. **Memory Access**: Array copying vs shared memory access trade-offs
3. **Context Switching**: High thread counts can cause scheduling overhead

## File Structure

- `project1.c` - Main implementation with 4 teams, signal handling, and quicksort
- `project1_signals.c` - Enhanced version with additional signal testing features
- `signal_tester.c` - Utility for sending specific signals to processes
- `simple_signal_test.sh` - Automated testing script with multiple test modes
- `better_test.sh` - Enhanced test suite with logging and performance analysis
- `Makefile` - Build configuration with test targets
- `README.md` - This documentation
