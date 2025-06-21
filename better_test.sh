#!/bin/bash

# ECE 434 Project 1 Enhanced Test Script with Comprehensive Logging
# This script provides detailed logging for analysis and report writing

# Configuration
LOG_DIR="test_logs"
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
MAIN_LOG="$LOG_DIR/test_session_$TIMESTAMP.log"
SIGNAL_LOG="$LOG_DIR/signal_tests_$TIMESTAMP.log"
PERF_LOG="$LOG_DIR/performance_$TIMESTAMP.log"
ERROR_LOG="$LOG_DIR/errors_$TIMESTAMP.log"

# Create log directory
mkdir -p "$LOG_DIR"

# Initialize logs
echo "=== ECE 434 Project 1 Test Session Started at $(date) ===" | tee "$MAIN_LOG"
echo "Test Environment: $(uname -a)" | tee -a "$MAIN_LOG"
echo "GCC Version: $(gcc --version | head -1)" | tee -a "$MAIN_LOG"
echo "" | tee -a "$MAIN_LOG"

# Logging functions
log_info() {
    echo "[INFO $(date +'%H:%M:%S')] $1" | tee -a "$MAIN_LOG"
}

log_error() {
    echo "[ERROR $(date +'%H:%M:%S')] $1" | tee -a "$MAIN_LOG" -a "$ERROR_LOG"
}

log_signal() {
    echo "[SIGNAL $(date +'%H:%M:%S')] $1" | tee -a "$MAIN_LOG" -a "$SIGNAL_LOG"
}

log_perf() {
    echo "[PERF $(date +'%H:%M:%S')] $1" | tee -a "$MAIN_LOG" -a "$PERF_LOG"
}

# Build verification
verify_build() {
    log_info "Verifying build environment..."
    
    if [ ! -f "./project1" ]; then
        log_info "Building project1..."
        make all 2>&1 | tee -a "$MAIN_LOG"
        
        if [ $? -ne 0 ]; then
            log_error "Build failed! Check compilation errors above."
            return 1
        fi
        log_info "Build successful"
    else
        log_info "project1 executable found"
    fi
    
    # Test signal_tester
    if [ ! -f "./signal_tester" ]; then
        log_info "signal_tester not found, creating minimal version..."
        create_minimal_signal_tester
    fi
    
    return 0
}

# Create minimal signal tester if needed
create_minimal_signal_tester() {
    cat > signal_tester.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <pid> <signal>\n", argv[0]);
        return 1;
    }
    
    pid_t pid = atoi(argv[1]);
    int sig = atoi(argv[2]);
    
    if (pid <= 0) {
        printf("Invalid PID: %d\n", pid);
        return 1;
    }
    
    printf("Sending signal %d to process %d\n", sig, pid);
    
    if (kill(pid, sig) == -1) {
        printf("Failed to send signal: %s\n", strerror(errno));
        return 1;
    }
    
    printf("Signal sent successfully.\n");
    return 0;
}
EOF

    gcc -o signal_tester signal_tester.c 2>&1 | tee -a "$MAIN_LOG"
    if [ $? -eq 0 ]; then
        log_info "Created minimal signal_tester successfully"
    else
        log_error "Failed to create signal_tester"
        return 1
    fi
}

# Enhanced process monitoring
monitor_process() {
    local pid=$1
    local test_name=$2
    local timeout_seconds=$3
    
    log_info "Monitoring process $pid for test: $test_name"
    
    local count=0
    while [ $count -lt $timeout_seconds ]; do
        if ! kill -0 "$pid" 2>/dev/null; then
            log_info "Process $pid terminated after $count seconds"
            return 1  # Process ended
        fi
        
        # Log process status every 5 seconds
        if [ $((count % 5)) -eq 0 ] && [ $count -gt 0 ]; then
            local cpu_usage=$(ps -p "$pid" -o %cpu --no-headers 2>/dev/null)
            local mem_usage=$(ps -p "$pid" -o %mem --no-headers 2>/dev/null)
            log_perf "PID $pid - CPU: ${cpu_usage}%, MEM: ${mem_usage}%"
        fi
        
        sleep 1
        ((count++))
    done
    
    log_info "Process $pid still running after $timeout_seconds seconds"
    return 0  # Process still running
}

# Enhanced signal testing with detailed logging
test_signal_behavior() {
    local signal_num=$1
    local signal_name=$2
    local array_size=${3:-5000}
    local threads_per_team=${4:-4}
    
    log_signal "=== Testing $signal_name (signal $signal_num) ==="
    
    # Start the program
    log_signal "Starting program: ./project1 $array_size $threads_per_team"
    ./project1 "$array_size" "$threads_per_team" > "test_logs/signal_${signal_num}_output.log" 2>&1 &
    local pid=$!
    
    log_signal "Program started with PID: $pid"
    
    # Wait for program to initialize
    sleep 2
    
    # Check if process started successfully
    if ! kill -0 "$pid" 2>/dev/null; then
        log_error "Program failed to start or terminated immediately"
        return 1
    fi
    
    log_signal "Process $pid confirmed running, sending $signal_name"
    
    # Record pre-signal state
    local pre_state=$(ps -p "$pid" -o state --no-headers 2>/dev/null | tr -d ' ')
    log_signal "Pre-signal process state: $pre_state"
    
    # Send the signal
    if ./signal_tester "$pid" "$signal_num" 2>&1 | tee -a "$SIGNAL_LOG"; then
        log_signal "Signal $signal_name sent successfully to PID $pid"
    else
        log_error "Failed to send signal $signal_name to PID $pid"
        kill -TERM "$pid" 2>/dev/null
        return 1
    fi
    
    # Monitor for 5 seconds to see the effect
    sleep 1
    
    local post_state=""
    local process_exists=true
    
    if kill -0 "$pid" 2>/dev/null; then
        post_state=$(ps -p "$pid" -o state --no-headers 2>/dev/null | tr -d ' ')
        log_signal "Post-signal process state: $post_state"
        log_signal "Process $pid survived signal $signal_name"
        
        # Wait a bit more to see if there are delayed effects
        sleep 3
        
        if kill -0 "$pid" 2>/dev/null; then
            log_signal "Process $pid still running 4 seconds after $signal_name"
            # Clean termination
            kill -TERM "$pid" 2>/dev/null
            sleep 1
            if kill -0 "$pid" 2>/dev/null; then
                kill -KILL "$pid" 2>/dev/null
                log_signal "Had to force-kill process $pid"
            fi
        else
            log_signal "Process $pid terminated within 4 seconds of $signal_name"
            process_exists=false
        fi
    else
        log_signal "Process $pid terminated immediately after $signal_name"
        process_exists=false
    fi
    
    # Capture any remaining output
    sleep 1
    
    # Analyze the output
    if [ -f "test_logs/signal_${signal_num}_output.log" ]; then
        local handler_count=$(grep -c "caught signal $signal_num" "test_logs/signal_${signal_num}_output.log" 2>/dev/null || echo "0")
        log_signal "Signal handlers triggered: $handler_count times for signal $signal_num"
        
        if [ "$handler_count" -gt 0 ]; then
            log_signal "Signal $signal_name was successfully caught and handled"
        else
            log_signal "Signal $signal_name was not caught by custom handlers"
        fi
        
        # Extract any team information
        grep "Team.*caught signal" "test_logs/signal_${signal_num}_output.log" 2>/dev/null | while read -r line; do
            log_signal "Handler details: $line"
        done
    fi
    
    # Summary
    log_signal "Summary for $signal_name: Process survived=$process_exists, Handlers triggered=$handler_count"
    log_signal "=== End $signal_name test ===\n"
    
    return 0
}

# Comprehensive signal test suite
run_comprehensive_signal_tests() {
    log_signal "=== COMPREHENSIVE SIGNAL TEST SUITE ==="
    
    # Define all signals to test with their expected behaviors
    declare -A signals=(
        ["2"]="SIGINT"
        ["6"]="SIGABRT" 
        ["4"]="SIGILL"
        ["17"]="SIGCHLD"
        ["11"]="SIGSEGV"
        ["8"]="SIGFPE"
        ["1"]="SIGHUP"
        ["20"]="SIGTSTP"
    )
    
    local total_tests=0
    local successful_tests=0
    local failed_tests=0
    
    for sig_num in "${!signals[@]}"; do
        local sig_name="${signals[$sig_num]}"
        
        ((total_tests++))
        
        if test_signal_behavior "$sig_num" "$sig_name" 5000 4; then
            ((successful_tests++))
            log_signal "✓ $sig_name test completed"
        else
            ((failed_tests++))
            log_error "✗ $sig_name test failed"
        fi
        
        # Small delay between tests
        sleep 2
    done
    
    # Final summary
    log_signal "=== SIGNAL TEST SUMMARY ==="
    log_signal "Total tests: $total_tests"
    log_signal "Successful: $successful_tests"
    log_signal "Failed: $failed_tests"
    log_signal "Success rate: $(echo "scale=2; $successful_tests * 100 / $total_tests" | bc -l)%"
}

# Enhanced performance testing with detailed metrics
run_performance_analysis() {
    log_perf "=== PERFORMANCE ANALYSIS SUITE ==="
    
    local test_configs=(
        "1000:4:Small-Few"
        "1000:100:Small-Many" 
        "10000:4:Medium-Few"
        "10000:100:Medium-Many"
        "50000:4:Large-Few"
        "50000:100:Large-Many"
    )
    
    for config in "${test_configs[@]}"; do
        IFS=':' read -r array_size threads name <<< "$config"
        
        log_perf "--- Performance Test: $name ---"
        log_perf "Configuration: Array=$array_size, Threads=$threads"
        
        # Run multiple iterations for statistical significance
        local total_time=0
        local iterations=3
        local successful_runs=0
        
        for ((i=1; i<=iterations; i++)); do
            log_perf "Run $i/$iterations for $name"
            
            local start_time=$(date +%s.%N)
            
            # Capture output and timing
            timeout 120s ./project1 "$array_size" "$threads" > "test_logs/perf_${name}_run${i}.log" 2>&1
            local exit_code=$?
            
            local end_time=$(date +%s.%N)
            local runtime=$(echo "$end_time - $start_time" | bc -l)
            
            if [ "$exit_code" -eq 0 ]; then
                ((successful_runs++))
                total_time=$(echo "$total_time + $runtime" | bc -l)
                log_perf "Run $i completed successfully in ${runtime}s"
                
                # Extract completion order if available
                if grep -q "COMPLETION ORDER" "test_logs/perf_${name}_run${i}.log"; then
                    log_perf "Completion order found in run $i:"
                    grep -A 5 "COMPLETION ORDER" "test_logs/perf_${name}_run${i}.log" | tee -a "$PERF_LOG"
                fi
            elif [ "$exit_code" -eq 124 ]; then
                log_error "Run $i timed out after 120s"
            else
                log_error "Run $i failed with exit code $exit_code"
            fi
        done
        
        # Calculate statistics
        if [ "$successful_runs" -gt 0 ]; then
            local avg_time=$(echo "scale=6; $total_time / $successful_runs" | bc -l)
            log_perf "$name Results: $successful_runs/$iterations successful, Average time: ${avg_time}s"
        else
            log_error "$name: All runs failed!"
        fi
        
        log_perf "--- End $name ---\n"
        sleep 2
    done
}

# System resource monitoring
monitor_system_resources() {
    local duration=${1:-30}
    log_info "Monitoring system resources for ${duration}s"
    
    for ((i=0; i<duration; i+=5)); do
        local cpu_usage=$(top -bn1 | grep "Cpu(s)" | sed "s/.*, *\([0-9.]*\)%* id.*/\1/" | awk '{print 100 - $1}')
        local mem_usage=$(free | grep Mem | awk '{printf("%.1f", $3/$2 * 100.0)}')
        local load_avg=$(uptime | awk -F'load average:' '{print $2}' | sed 's/^[ \t]*//')
        
        log_perf "System: CPU ${cpu_usage}%, MEM ${mem_usage}%, Load${load_avg}"
        sleep 5
    done
}

# Generate comprehensive report
generate_test_report() {
    local report_file="$LOG_DIR/test_report_$TIMESTAMP.md"
    
    log_info "Generating comprehensive test report: $report_file"
    
    cat > "$report_file" << EOF
# ECE 434 Project 1 - Test Report

**Generated:** $(date)  
**Test Session:** $TIMESTAMP

## Test Environment
- **System:** $(uname -a)
- **Compiler:** $(gcc --version | head -1)
- **Test Duration:** $(date +%s) seconds

## Signal Testing Results

EOF

    # Add signal test analysis
    if [ -f "$SIGNAL_LOG" ]; then
        echo "### Signal Handler Analysis" >> "$report_file"
        echo "" >> "$report_file"
        
        grep "Summary for" "$SIGNAL_LOG" | while read -r line; do
            echo "- $line" >> "$report_file"
        done
        
        echo "" >> "$report_file"
        echo "### Detailed Signal Logs" >> "$report_file"
        echo "\`\`\`" >> "$report_file"
        tail -50 "$SIGNAL_LOG" >> "$report_file"
        echo "\`\`\`" >> "$report_file"
    fi
    
    # Add performance analysis
    if [ -f "$PERF_LOG" ]; then
        echo "" >> "$report_file"
        echo "## Performance Analysis" >> "$report_file"
        echo "" >> "$report_file"
        
        grep "Results:" "$PERF_LOG" | while read -r line; do
            echo "- $line" >> "$report_file"
        done
    fi
    
    # Add error summary
    if [ -f "$ERROR_LOG" ]; then
        echo "" >> "$report_file"
        echo "## Issues Encountered" >> "$report_file"
        echo "" >> "$report_file"
        echo "\`\`\`" >> "$report_file"
        cat "$ERROR_LOG" >> "$report_file"
        echo "\`\`\`" >> "$report_file"
    fi
    
    log_info "Test report generated: $report_file"
}

# Enhanced main menu with logging options
show_enhanced_menu() {
    echo ""
    echo "=== Enhanced Test Suite with Logging ==="
    echo "1. Quick functionality test"
    echo "2. Comprehensive signal analysis" 
    echo "3. Performance analysis suite"
    echo "4. Custom test with monitoring"
    echo "5. System resource monitoring"
    echo "6. Generate test report"
    echo "7. View recent logs"
    echo "8. Clean old logs"
    echo "9. Exit"
    echo ""
    echo "Logs are saved in: $LOG_DIR"
    read -p "Choose option (1-9): " choice
    
    case $choice in
        1)
            verify_build && {
                log_info "Running quick functionality test"
                ./project1 1000 4 2>&1 | tee "test_logs/quick_test_$TIMESTAMP.log"
            }
            ;;
        2)
            verify_build && run_comprehensive_signal_tests
            ;;
        3)
            verify_build && run_performance_analysis
            ;;
        4)
            read -p "Array size: " asize
            read -p "Threads per team: " tcount
            read -p "Test name: " tname
            verify_build && {
                log_info "Running custom test: $tname"
                ./project1 "$asize" "$tcount" 2>&1 | tee "test_logs/custom_${tname}_$TIMESTAMP.log"
            }
            ;;
        5)
            read -p "Monitor duration (seconds): " duration
            monitor_system_resources "$duration"
            ;;
        6)
            generate_test_report
            ;;
        7)
            echo "Recent log files:"
            ls -lt "$LOG_DIR"/*.log 2>/dev/null | head -10
            read -p "View which log? (filename): " logfile
            if [ -f "$LOG_DIR/$logfile" ]; then
                less "$LOG_DIR/$logfile"
            else
                echo "Log file not found"
            fi
            ;;
        8)
            read -p "Delete logs older than how many days? " days
            find "$LOG_DIR" -name "*.log" -mtime +"$days" -delete
            log_info "Cleaned logs older than $days days"
            ;;
        9)
            log_info "Test session ended"
            generate_test_report
            echo "All logs saved in: $LOG_DIR"
            exit 0
            ;;
        *)
            echo "Invalid option!"
            ;;
    esac
}

# Main execution
echo "ECE 434 Project 1 - Enhanced Testing Framework"
echo "=============================================="

# Check dependencies
if ! command -v bc &> /dev/null; then
    log_error "bc calculator not found. Install with: sudo apt-get install bc"
    exit 1
fi

# Initial build verification
if ! verify_build; then
    log_error "Build verification failed. Cannot proceed with tests."
    exit 1
fi

# Command line mode or interactive mode
if [ $# -eq 0 ]; then
    # Interactive mode
    while true; do
        show_enhanced_menu
    done
else
    # Command line mode
    case $1 in
        "quick")
            ./project1 1000 4 2>&1 | tee "test_logs/quick_test_$TIMESTAMP.log"
            ;;
        "signals")
            run_comprehensive_signal_tests
            ;;
        "perf")
            run_performance_analysis
            ;;
        "report")
            generate_test_report
            ;;
        *)
            echo "Usage: $0 [quick|signals|perf|report]"
            echo "Or run without arguments for interactive mode"
            ;;
    esac
    
    # Always generate report at the end
    generate_test_report
fi