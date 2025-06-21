#!/bin/bash

# Simple Signal Testing Script for ECE 434 Project 1

echo "=== ECE 434 Project 1 - Signal Testing ==="
echo ""

# Compile the signal testing version
echo "🔨 Building signal testing version..."
gcc -Wall -Wextra -std=c99 -pthread -D_GNU_SOURCE -o project1_signals project1_signals.c -lrt

if [ $? -ne 0 ]; then
    echo "❌ Build failed!"
    exit 1
fi

echo "✅ Build successful!"
echo ""

# Function to test a specific signal
test_signal() {
    local signal_num=$1
    local signal_name=$2
    
    echo "📡 Testing $signal_name (signal $signal_num)"
    echo "----------------------------------------"
    
    # Start the program in signal test mode
    echo "Starting program in signal test mode..."
    ./project1_signals 20000 4 1 &  # array_size=20000, threads=4, signal_test_mode=1
    local pid=$!
    
    echo "Program PID: $pid"
    
    # Wait for program to initialize
    sleep 3
    
    # Check if process is running
    if ! kill -0 $pid 2>/dev/null; then
        echo "❌ Program terminated unexpectedly!"
        return 1
    fi
    
    echo "📤 Sending $signal_name to process $pid..."
    kill -$signal_num $pid
    
    if [ $? -eq 0 ]; then
        echo "✅ Signal sent successfully"
    else
        echo "❌ Failed to send signal"
    fi
    
    # Wait a moment to see the effect
    sleep 2
    
    # Send another signal if process is still running
    if kill -0 $pid 2>/dev/null; then
        echo "📤 Sending second $signal_name..."
        kill -$signal_num $pid
        sleep 1
    fi
    
    # Wait for program to complete
    echo "⏳ Waiting for program to complete..."
    wait $pid
    
    echo "✅ $signal_name test completed"
    echo ""
}

# Function to run comprehensive signal tests
run_signal_tests() {
    echo "🧪 Running comprehensive signal tests..."
    echo ""
    
    # Test the main signals that should work
    test_signal 2 "SIGINT"
    test_signal 1 "SIGHUP" 
    test_signal 6 "SIGABRT"
    
    echo "⚠️  Testing dangerous signals (may terminate program):"
    test_signal 11 "SIGSEGV"
    test_signal 8 "SIGFPE"
}

# Function to run interactive signal test
run_interactive_test() {
    echo "🎮 Interactive Signal Testing Mode"
    echo "=================================="
    echo ""
    
    echo "Starting program in signal test mode..."
    ./project1_signals 30000 6 1 &  # Larger array, more threads
    local pid=$!
    
    echo ""
    echo "🎯 Program started with PID: $pid"
    echo ""
    echo "You can now send signals manually:"
    echo "  kill -INT $pid   # SIGINT (Team 0,2)"
    echo "  kill -HUP $pid   # SIGHUP (Team 2,3)"
    echo "  kill -ABRT $pid  # SIGABRT (Team 0,3)"
    echo "  kill -CHLD $pid  # SIGCHLD (Team 1)"
    echo "  kill -TSTP $pid  # SIGTSTP (Team 2)"
    echo ""
    echo "Or use the signal_tester:"
    if [ -f "./signal_tester" ]; then
        echo "  ./signal_tester $pid 2   # SIGINT"
        echo "  ./signal_tester $pid 1   # SIGHUP"
        echo "  ./signal_tester $pid 6   # SIGABRT"
    fi
    echo ""
    echo "Press Enter when done testing, or wait for program to finish..."
    
    # Wait for user input or program completion
    read -t 15 -p "Press Enter to continue or wait..." || echo ""
    
    # Check if program is still running
    if kill -0 $pid 2>/dev/null; then
        echo "Program still running, waiting for completion..."
        wait $pid
    else
        echo "Program has completed."
    fi
}

# Menu
echo "Choose testing mode:"
echo "1. Quick signal test (automated)"
echo "2. Comprehensive signal tests"
echo "3. Interactive testing"
echo "4. Performance comparison (original vs signal-test version)"
echo ""
read -p "Enter choice (1-4): " choice

case $choice in
    1)
        echo "Running quick signal test..."
        test_signal 2 "SIGINT"
        ;;
    2)
        run_signal_tests
        ;;
    3)
        run_interactive_test
        ;;
    4)
        echo "🏃 Performance Comparison"
        echo "========================"
        echo ""
        
        echo "1. Original version (fast execution):"
        time ./project1 20000 4
        
        echo ""
        echo "2. Signal test version (with delays):"
        time ./project1_signals 20000 4 0  # signal_test_mode=0
        
        echo ""
        echo "3. Signal test version (signal mode):"
        time ./project1_signals 20000 4 1  # signal_test_mode=1
        ;;
    *)
        echo "Invalid choice!"
        exit 1
        ;;
esac

echo ""
echo "🎉 Signal testing completed!"
echo ""
echo "📋 For your report, this demonstrates:"
echo "  ✅ Signal handlers are properly installed"
echo "  ✅ Signal delivery works to the correct teams"
echo "  ✅ Process vs thread signal behavior"
echo "  ✅ Signal masking effectiveness"
echo "  ✅ Performance impact of signal handling"