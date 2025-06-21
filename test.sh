#!/bin/bash

# ECE 434 Project 1 Test Script
# This script helps automate testing of the signal handling and sorting program

echo "=== ECE 434 Project 1 Test Script ==="

# Check if programs are built
if [ ! -f "./project1" ]; then
    echo "Building project1..."
    make all
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi
fi

# Function to run a test case
run_test_case() {
    local array_size=$1
    local threads_per_team=$2
    local test_name=$3
    
    echo ""
    echo "--- Running $test_name ---"
    echo "Array size: $array_size, Threads per team: $threads_per_team"
    echo "Starting program..."
    
    # Run the program in background and capture PID
    timeout 30s ./project1 $array_size $threads_per_team &
    local pid=$!
    
    echo "Program PID: $pid"
    sleep 2  # Let the program start up
    
    # Check if process is still running
    if ! kill -0 $pid 2>/dev/null; then
        echo "Program terminated early!"
        return 1
    fi
    
    echo "Program is running. You can send signals using:"
    echo "  ./signal_tester $pid <signal_number>"
    echo "  kill -<signal> $pid"
    echo ""
    echo "Available signals:"
    echo "  SIGINT(2), SIGABRT(6), SIGILL(4), SIGCHLD(17)"
    echo "  SIGSEGV(11), SIGFPE(8), SIGHUP(1), SIGTSTP(20)"
    
    # Wait for program to complete
    wait $pid
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo "$test_name completed successfully!"
    elif [ $exit_code -eq 124 ]; then
        echo "$test_name timed out (30 seconds)"
    else
        echo "$test_name failed with exit code $exit_code"
    fi
    
    return $exit_code
}

# Function to run automated signal tests
run_signal_tests() {
    local array_size=5000
    local threads_per_team=4
    
    echo ""
    echo "=== Automated Signal Tests ==="
    
    # Test each signal individually
    local signals=(2 6 4 17 11 8 1 20)  # SIGINT, SIGABRT, SIGILL, SIGCHLD, SIGSEGV, SIGFPE, SIGHUP, SIGTSTP
    local signal_names=("SIGINT" "SIGABRT" "SIGILL" "SIGCHLD" "SIGSEGV" "SIGFPE" "SIGHUP" "SIGTSTP")
    
    for i in "${!signals[@]}"; do
        local sig=${signals[$i]}
        local name=${signal_names[$i]}
        
        echo ""
        echo "--- Testing $name (signal $sig) ---"
        
        # Start program
        ./project1 $array_size $threads_per_team &
        local pid=$!
        
        sleep 1  # Let it start
        
        if kill -0 $pid 2>/dev/null; then
            echo "Sending $name to PID $pid"
            ./signal_tester $pid $sig
            sleep 1
            
            # Check if still running
            if kill -0 $pid 2>/dev/null; then
                echo "$name: Process still running (signal was handled)"
                kill -TERM $pid 2>/dev/null  # Clean termination
                wait $pid 2>/dev/null
            else
                echo "$name: Process terminated (signal caused termination)"
            fi
        else
            echo "Failed to start program for $name test"
        fi
        
        sleep 1
    done
}

# Function to run performance comparison tests
run_performance_tests() {
    echo ""
    echo "=== Performance Comparison Tests ==="
    
    local test_configs=(
        "1000 4 Small-Few"
        "1000 100 Small-Many" 
        "10000 4 Medium-Few"
        "10000 100 Medium-Many"
        "100000 4 Large-Few"
        "100000 100 Large-Many"
    )
    
    for config in "${test_configs[@]}"; do
        local params=($config)
        local array_size=${params[0]}
        local threads=${params[1]} 
        local name=${params[2]}
        
        echo ""
        echo "--- Performance Test: $name ---"
        echo "Running: ./project1 $array_size $threads"
        
        # Run with timing
        time timeout 60s ./project1 $array_size $threads
        local exit_code=$?
        
        if [ $exit_code -eq 124 ]; then
            echo "WARNING: $name test timed out!"
        elif [ $exit_code -ne 0 ]; then
            echo "WARNING: $name test failed!"
        fi
        
        sleep 2
    done
}

# Main menu
show_menu() {
    echo ""
    echo "=== Test Options ==="
    echo "1. Quick test (small array)"
    echo "2. Medium test"  
    echo "3. Large test"
    echo "4. All performance tests"
    echo "5. Interactive signal test"
    echo "6. Automated signal tests"
    echo "7. Exit"
    echo ""
    read -p "Choose an option (1-7): " choice
    
    case $choice in
        1)
            run_test_case 1000 4 "Quick Test"
            ;;
        2)
            run_test_case 10000 100 "Medium Test"
            ;;
        3)
            run_test_case 100000 1000 "Large Test"
            ;;
        4)
            run_performance_tests
            ;;
        5)
            echo "Enter array size and threads per team:"
            read -p "Array size: " asize
            read -p "Threads per team: " tcount
            run_test_case $asize $tcount "Interactive Test"
            ;;
        6)
            run_signal_tests
            ;;
        7)
            echo "Exiting..."
            exit 0
            ;;
        *)
            echo "Invalid option!"
            ;;
    esac
}

# Check for command line arguments
if [ $# -eq 0 ]; then
    # Interactive mode
    while true; do
        show_menu
    done
else
    # Command line mode
    case $1 in
        "quick")
            run_test_case 1000 4 "Quick Test"
            ;;
        "medium")
            run_test_case 10000 100 "Medium Test"
            ;;
        "large")
            run_test_case 100000 1000 "Large Test"
            ;;
        "perf")
            run_performance_tests
            ;;
        "signals")
            run_signal_tests
            ;;
        *)
            echo "Usage: $0 [quick|medium|large|perf|signals]"
            echo "Or run without arguments for interactive mode"
            ;;
    esac
fi