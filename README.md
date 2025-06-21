# ECE 434 Project 3: Thread Teams with Signal Handling

## Overview
This project implements a multi-threaded program with 4 teams of pthread threads. Each team handles specific signals and sorts a portion of a large integer array.

## Features Implemented
- **Action 1**: 4 teams of threads with custom signal handlers
- **Action 2**: Parallel array sorting using Case 1 (sequential quicksort)
- **Action 3**: Signal-based termination (to be implemented)
- **Action 4**: Signal behavior experiments

## Signal Assignment by Team
- **Team 0**: SIGINT, SIGABRT, SIGILL
- **Team 1**: SIGCHLD, SIGSEGV, SIGFPE  
- **Team 2**: SIGHUP, SIGTSTP, SIGINT
- **Team 3**: SIGABRT, SIGFPE, SIGHUP


## Building the Project

```bash
# Compile everything
make all

# Clean build files
make clean
```

## Running the Program

### Basic Usage
```bash
# Default: 10,000 elements, 4 threads per team
./project1

# Custom array size and threads per team
./project1 <array_size> <threads_per_team>

# Examples:
./project1 1000 4      # Small test
./project1 100000 100  # Large test
```

### Pre-defined Test Cases
```bash
make test_small   # 1,000 elements, 4 threads per team
make test_medium  # 10,000 elements, 100 threads per team  
make test_large   # 100,000 elements, 1,000 threads per team
make test_all     # Run all three test cases
```

## Testing Signal Handling

### Step 1: Run the main program
```bash
./project1 50000 10 &
echo $!  # Note the process ID
```

### Step 2: Send signals using the tester
```bash
# Send SIGINT (signal 2) to the process
./signal_tester <pid> 2

# Send SIGABRT (signal 6) to the process  
./signal_tester <pid> 6

# Send SIGHUP (signal 1) to the process
./signal_tester <pid> 1
```

### Manual signal sending
```bash
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
- Each team blocks signals not assigned to them
- Signal handlers print team ID and thread ID for identification

### Sorting Implementation  
- **Case 1**: Sequential quicksort implementation
- Only the first thread in each team performs sorting (representative thread)
- Other threads wait for completion
- Timing measurements using `clock_gettime()`

### Thread Management
- Teams are independent pthread groups
- Completion tracking with mutex protection
- Clean thread joining and resource cleanup

## Known Issues and Considerations

### Signal Delivery Challenges
1. **Process vs Thread Signals**: Some signals (like SIGINT from Ctrl+C) go to the entire process
2. **Dangerous Signals**: SIGSEGV and SIGFPE can cause immediate termination
3. **Signal Masking**: Complex interaction between pthread signal masks

### Performance Considerations
1. **Thread Overhead**: Many threads may not improve performance for small arrays
2. **Memory Access**: Array copying vs shared memory access trade-offs
3. **Context Switching**: High thread counts can cause scheduling overhead

## Files Structure

- `project1.c` - Main implementation
- `signal_tester.c` - Signal testing utility
- `Makefile` - Build configuration
- `README.md` - This documentation

### Common Issues
1. **Permission Denied**: Run with appropriate privileges for signal sending
