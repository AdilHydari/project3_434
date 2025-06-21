#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>

// Configuration constants
#define NUM_TEAMS 4
#define DEFAULT_ARRAY_SIZE 10000
#define DEFAULT_THREADS_PER_TEAM 4

// Global state
int *main_array;
int array_size = DEFAULT_ARRAY_SIZE;
int padded_array_size;
int threads_per_team = DEFAULT_THREADS_PER_TEAM;
int completion_order[NUM_TEAMS] = {-1, -1, -1, -1};
int completion_index = 0;
pthread_mutex_t completion_mutex = PTHREAD_MUTEX_INITIALIZER;

// Bitonic sort synchronization
pthread_barrier_t global_barrier;
int sort_completed = 0;

// Team data structure
typedef struct {
    int team_id;
    int *subarray;
    int subarray_size;
    int start_index;
    pthread_t *threads;
    int num_threads;
    struct timespec start_time;
    struct timespec end_time;
    int completed;
} team_data_t;

team_data_t teams[NUM_TEAMS];

// Signal configuration for each team
// Each team handles 3 different signals
int team_signals[NUM_TEAMS][3] = {
    {SIGINT, SIGABRT, SIGILL},      // Team 0
    {SIGCHLD, SIGSEGV, SIGFPE},     // Team 1  
    {SIGHUP, SIGTSTP, SIGINT},      // Team 2
    {SIGABRT, SIGFPE, SIGHUP}       // Team 3
};

void signal_handler(int sig) {
    pthread_t current_thread = pthread_self();
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timestamp[26];
    strftime(timestamp, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    
    int team_id = -1;
    int thread_index = -1;
    
    // Find which team this thread belongs to
    for (int i = 0; i < NUM_TEAMS && team_id == -1; i++) {
        for (int j = 0; j < teams[i].num_threads; j++) {
            if (pthread_equal(current_thread, teams[i].threads[j])) {
                team_id = i;
                thread_index = j;
                break;
            }
        }
    }
    
    printf("[SIGNAL %s] Team %d, Thread %d caught signal %d (%s)\n", 
           timestamp, team_id, thread_index, sig, strsignal(sig));
    
    // Check if this signal should be handled by this team
    int should_handle = 0;
    if (team_id >= 0) {
        for (int i = 0; i < 3; i++) {
            if (team_signals[team_id][i] == sig) {
                should_handle = 1;
                break;
            }
        }
    }
    
    if (should_handle) {
        printf("[SIGNAL %s] ✓ Signal %d handled correctly by Team %d\n", timestamp, sig, team_id);
    } else {
        printf("[SIGNAL %s] ⚠ Signal %d received by Team %d (not assigned)\n", timestamp, sig, team_id);
    }
    
    fflush(stdout);
}

// Function declarations
void bitonic_compare_and_swap(int *arr, int i, int j, int ascending);
void bitonic_merge(int *arr, int start, int length, int ascending, int thread_id, int num_threads);
void bitonic_sort_parallel(int *arr, int start, int length, int ascending, int thread_id, int num_threads);
int next_power_of_2(int n);

int next_power_of_2(int n) {
    int power = 1;
    while (power < n) {
        power *= 2;
    }
    return power;
}

void bitonic_compare_and_swap(int *arr, int i, int j, int ascending) {
    if ((arr[i] > arr[j]) == ascending) {
        int temp = arr[i];
        arr[i] = arr[j];
        arr[j] = temp;
    }
}

void bitonic_merge(int *arr, int start, int length, int ascending, int thread_id, int num_threads) {
    if (length <= 1) return;
    
    int half = length / 2;
    
    // Calculate work distribution for this thread for compare-and-swap phase
    int work_per_thread = (half + num_threads - 1) / num_threads;
    int thread_start = thread_id * work_per_thread;
    int thread_end = thread_start + work_per_thread;
    if (thread_end > half) thread_end = half;
    
    // All threads participate in parallel compare-and-swap phase
    for (int i = thread_start; i < thread_end; i++) {
        bitonic_compare_and_swap(arr, start + i, start + i + half, ascending);
    }
    
    // Synchronize all threads after compare-and-swap
    pthread_barrier_wait(&global_barrier);
    
    // All threads recursively merge both halves
    bitonic_merge(arr, start, half, ascending, thread_id, num_threads);
    bitonic_merge(arr, start + half, half, ascending, thread_id, num_threads);
}

void bitonic_sort_parallel(int *arr, int start, int length, int ascending, int thread_id, int num_threads) {
    if (length <= 1) return;
    
    int half = length / 2;
    
    // All threads work on both halves but in opposite directions
    bitonic_sort_parallel(arr, start, half, 1, thread_id, num_threads);
    bitonic_sort_parallel(arr, start + half, half, 0, thread_id, num_threads);
    
    // Synchronize before merge
    pthread_barrier_wait(&global_barrier);
    
    // Merge the two bitonic sequences
    bitonic_merge(arr, start, length, ascending, thread_id, num_threads);
}
void setup_team_signals(int team_id) {
    if (team_id < 0 || team_id >= NUM_TEAMS) {
        printf("[ERROR] Invalid team_id %d\n", team_id);
        return;
    }
    
    sigset_t block_set;
    sigemptyset(&block_set);
    
    int signals_blocked = 0;
    
    // Block signals handled by other teams (but not by this team)
    for (int other_team = 0; other_team < NUM_TEAMS; other_team++) {
        if (other_team == team_id) continue;
        
        for (int i = 0; i < 3; i++) {
            int other_signal = team_signals[other_team][i];
            
            // Check if current team also handles this signal
            int handled_by_current_team = 0;
            for (int j = 0; j < 3; j++) {
                if (other_signal == team_signals[team_id][j]) {
                    handled_by_current_team = 1;
                    break;
                }
            }
            
            if (!handled_by_current_team) {
                sigaddset(&block_set, other_signal);
                signals_blocked++;
            }
        }
    }
    
    if (pthread_sigmask(SIG_BLOCK, &block_set, NULL) != 0) {
        printf("[ERROR] Team %d: Failed to block signals: %s\n", team_id, strerror(errno));
    } else {
        printf("[SETUP] Team %d: Blocked %d signals from other teams\n", 
               team_id, signals_blocked);
    }
}

void* bitonic_thread_function(void* arg) {
    team_data_t *team = (team_data_t*)arg;
    pthread_t self = pthread_self();
    
    // Find thread index within team
    int thread_index = -1;
    for (int i = 0; i < team->num_threads; i++) {
        if (pthread_equal(self, team->threads[i])) {
            thread_index = i;
            break;
        }
    }
    
    if (thread_index == -1) {
        printf("[ERROR] Could not identify thread in team %d\n", team->team_id);
        return NULL;
    }
    
    printf("[BITONIC] Team %d Thread %d starting (array size: %d)\n", 
           team->team_id, thread_index, padded_array_size);
    
    setup_team_signals(team->team_id);
    
    // Calculate global thread ID
    int global_thread_id = team->team_id * team->num_threads + thread_index;
    int total_threads = NUM_TEAMS * threads_per_team;
    
    printf("[BITONIC] Global thread %d (Team %d, Local %d) ready for parallel sorting\n", 
           global_thread_id, team->team_id, thread_index);
    
    // Record start time (only first thread)
    if (global_thread_id == 0) {
        clock_gettime(CLOCK_MONOTONIC, &team->start_time);
        printf("[BITONIC] Starting parallel bitonic sort with %d threads\n", total_threads);
    }
    
    // All threads participate in parallel bitonic sort
    bitonic_sort_parallel(main_array, 0, padded_array_size, 1, global_thread_id, total_threads);
    
    // Record completion time and verify (only first thread)
    if (global_thread_id == 0) {
        clock_gettime(CLOCK_MONOTONIC, &team->end_time);
        
        pthread_mutex_lock(&completion_mutex);
        sort_completed = 1;
        completion_order[0] = -1; // All teams collaborated
        completion_index = 1;
        
        double elapsed = (team->end_time.tv_sec - team->start_time.tv_sec) + 
                        (team->end_time.tv_nsec - team->start_time.tv_nsec) / 1e9;
        
        printf("[COMPLETED] Parallel bitonic sort finished in %.6f seconds\n", elapsed);
        pthread_mutex_unlock(&completion_mutex);
        
        // Verify sort correctness
        int is_sorted = 1;
        for (int i = 1; i < array_size; i++) {
            if (main_array[i-1] > main_array[i]) {
                is_sorted = 0;
                printf("[VERIFY ERROR] Position %d: %d > %d\n", i, main_array[i-1], main_array[i]);
                break;
            }
        }
        printf("[VERIFY] Bitonic sort verification: %s\n", is_sorted ? "PASSED" : "FAILED");
        
        // Show sample of sorted array
        printf("[RESULT] Sample sorted array: ");
        int sample_size = (array_size < 20) ? array_size : 20;
        for (int i = 0; i < sample_size; i++) {
            printf("%d ", main_array[i]);
        }
        if (array_size > 20) printf("...");
        printf("\n");
    }
    
    // Mark team completion for this thread's team
    team->completed = 1;
    
    // All threads wait for global completion
    while (!sort_completed) {
        usleep(1000);
    }
    
    printf("[BITONIC] Team %d Thread %d exiting\n", team->team_id, thread_index);
    return NULL;
}

void setup_signal_handlers() {
    printf("[SETUP] Setting up signal handlers\n");
    
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    
    int all_signals[] = {SIGINT, SIGABRT, SIGILL, SIGCHLD, SIGSEGV, SIGFPE, SIGHUP, SIGTSTP};
    int num_signals = sizeof(all_signals) / sizeof(all_signals[0]);
    
    for (int i = 0; i < num_signals; i++) {
        int sig = all_signals[i];
        if (sigaction(sig, &sa, NULL) == -1) {
            printf("[ERROR] Failed to set handler for signal %d: %s\n", 
                   sig, strerror(errno));
        } else {
            printf("[SETUP] Handler set for signal %d (%s)\n", 
                   sig, strsignal(sig));
        }
    }
}


void initialize_array() {
    // Calculate padded size for bitonic sort (must be power of 2)
    padded_array_size = next_power_of_2(array_size);
    
    printf("[INIT] Original array size: %d, Padded to: %d (power of 2)\n", 
           array_size, padded_array_size);
    
    main_array = malloc(padded_array_size * sizeof(int));
    if (!main_array) {
        printf("[ERROR] Failed to allocate memory: %s\n", strerror(errno));
        exit(1);
    }
    
    srand(time(NULL));
    
    // Fill original array with random values
    for (int i = 0; i < array_size; i++) {
        main_array[i] = rand() % 10000;
    }
    
    // Pad with maximum values to ensure they sort to the end
    for (int i = array_size; i < padded_array_size; i++) {
        main_array[i] = INT_MAX;
    }
    
    printf("[INIT] Generated %d random integers, padded with %d max values\n", 
           array_size, padded_array_size - array_size);
}

void create_teams() {
    printf("[INIT] Creating %d teams with %d threads each for parallel bitonic sort\n", NUM_TEAMS, threads_per_team);
    
    // Initialize global barrier for thread synchronization
    int total_threads = NUM_TEAMS * threads_per_team;
    if (pthread_barrier_init(&global_barrier, NULL, total_threads) != 0) {
        printf("[ERROR] Failed to initialize global barrier: %s\n", strerror(errno));
        exit(1);
    }
    printf("[INIT] Global barrier initialized for %d threads\n", total_threads);
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        teams[i].team_id = i;
        teams[i].num_threads = threads_per_team;
        teams[i].subarray = NULL; // No longer using subarrays
        teams[i].subarray_size = 0;
        teams[i].start_index = 0;
        teams[i].completed = 0;
        
        printf("[INIT] Team %d handles signals [%d(%s), %d(%s), %d(%s)]\n", 
               i, 
               team_signals[i][0], strsignal(team_signals[i][0]),
               team_signals[i][1], strsignal(team_signals[i][1]),
               team_signals[i][2], strsignal(team_signals[i][2]));
        
        teams[i].threads = malloc(threads_per_team * sizeof(pthread_t));
        if (!teams[i].threads) {
            printf("[ERROR] Failed to allocate threads for team %d: %s\n", 
                   i, strerror(errno));
            exit(1);
        }
        
        printf("[INIT] Team %d: %d threads ready for global array collaboration\n", 
               i, threads_per_team);
    }
}

void print_status() {
    printf("\n=== CONFIGURATION ===\n");
    printf("Array size: %d elements\n", array_size);
    printf("Teams: %d\n", NUM_TEAMS);
    printf("Threads per team: %d\n", threads_per_team);
    
    printf("\nSignal assignments:\n");
    for (int i = 0; i < NUM_TEAMS; i++) {
        printf("  Team %d: %d(%s), %d(%s), %d(%s)\n", 
               i, 
               team_signals[i][0], strsignal(team_signals[i][0]),
               team_signals[i][1], strsignal(team_signals[i][1]),
               team_signals[i][2], strsignal(team_signals[i][2]));
    }
    printf("=====================\n\n");
}

int main(int argc, char *argv[]) {
    printf("=== ECE 434 Project 1: Thread Teams with Signal Handling ===\n");
    printf("Process PID: %d\n", getpid());
    
    // Parse command line arguments
    if (argc > 1) {
        array_size = atoi(argv[1]);
        if (array_size <= 0 || array_size > 10000000) {
            printf("[ERROR] Invalid array size: %d\n", array_size);
            return 1;
        }
    }
    if (argc > 2) {
        threads_per_team = atoi(argv[2]);
        if (threads_per_team <= 0 || threads_per_team > 10000) {
            printf("[ERROR] Invalid threads per team: %d\n", threads_per_team);
            return 1;
        }
    }
    
    printf("[CONFIG] Array: %d elements, Threads per team: %d\n", array_size, threads_per_team);
    
    int total_threads = NUM_TEAMS * threads_per_team;
    if (total_threads > 1000) {
        printf("[WARNING] High thread count (%d) may impact performance\n", total_threads);
    }
    
    // Block all signals in main initially
    sigset_t block_all, old_mask;
    sigfillset(&block_all);
    if (pthread_sigmask(SIG_BLOCK, &block_all, &old_mask) != 0) {
        printf("[ERROR] Failed to block signals in main: %s\n", strerror(errno));
        return 1;
    }
    printf("[SETUP] All signals blocked in main thread\n");
    
    // Initialize array and teams
    initialize_array();
    create_teams();
    print_status();
    
    // Setup signal handlers (process-wide)
    setup_signal_handlers();
    
    // Create and start teams
    struct timespec program_start, program_end;
    clock_gettime(CLOCK_MONOTONIC, &program_start);
    
    printf("[STARTING] Creating %d teams...\n", NUM_TEAMS);
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        printf("[TEAM %d] Creating %d threads...\n", i, teams[i].num_threads);
        
        for (int j = 0; j < teams[i].num_threads; j++) {
            int result = pthread_create(&teams[i].threads[j], NULL, 
                                      bitonic_thread_function, &teams[i]);
            if (result != 0) {
                printf("[ERROR] Failed to create thread %d for team %d: %s\n", 
                       j, i, strerror(result));
                return 1;
            }
        }
        
        printf("[TEAM %d] All %d threads created successfully\n", i, teams[i].num_threads);
        
        // Small delay between team creation to see startup clearly
        usleep(100000); // 100ms
    }
    
    printf("[READY] All teams created. Ready to receive signals!\n");
    printf("[INFO] Send signals using: kill -<signal> %d\n", getpid());
    printf("[INFO] Or use: ./signal_tester %d <signal_number>\n", getpid());
    printf("[INFO] Available signals: SIGINT(2), SIGABRT(6), SIGILL(4), SIGCHLD(17), SIGSEGV(11), SIGFPE(8), SIGHUP(1), SIGTSTP(20)\n");
    
    // Wait for all teams to complete
    int teams_joined = 0;
    for (int i = 0; i < NUM_TEAMS; i++) {
        printf("[JOINING] Waiting for team %d threads to complete...\n", i);
        
        for (int j = 0; j < teams[i].num_threads; j++) {
            void *thread_result;
            int result = pthread_join(teams[i].threads[j], &thread_result);
            if (result != 0) {
                printf("[ERROR] Failed to join thread %d of team %d: %s\n", 
                       j, i, strerror(result));
            }
        }
        
        teams_joined++;
        printf("[JOINED] Team %d completed (%d/%d teams done)\n", i, teams_joined, NUM_TEAMS);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &program_end);
    double total_time = (program_end.tv_sec - program_start.tv_sec) + 
                       (program_end.tv_nsec - program_start.tv_nsec) / 1e9;
    
    // Print final results
    printf("\n=== FINAL RESULTS ===\n");
    printf("Total execution time: %.6f seconds\n", total_time);
    
    if (sort_completed) {
        double sort_time = (teams[0].end_time.tv_sec - teams[0].start_time.tv_sec) + 
                          (teams[0].end_time.tv_nsec - teams[0].start_time.tv_nsec) / 1e9;
        
        printf("Parallel bitonic sort results:\n");
        printf("  Algorithm: Parallel Bitonic Sort\n");
        printf("  Total threads: %d (across %d teams)\n", NUM_TEAMS * threads_per_team, NUM_TEAMS);
        printf("  Array size: %d elements (padded to %d)\n", array_size, padded_array_size);
        printf("  Sort time: %.6f seconds\n", sort_time);
        printf("  Elements per second: %.0f\n", array_size / sort_time);
        printf("  Parallel efficiency: All %d threads collaborated\n", NUM_TEAMS * threads_per_team);
    } else {
        printf("[ERROR] Sort did not complete successfully\n");
    }
    
    // Restore default signal handlers
    printf("\n[CLEANUP] Restoring default signal handlers...\n");
    signal(SIGINT, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGHUP, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    
    // Restore original signal mask
    pthread_sigmask(SIG_SETMASK, &old_mask, NULL);
    
    // Cleanup
    pthread_barrier_destroy(&global_barrier);
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        free(teams[i].threads);
    }
    free(main_array);
    
    printf("\n=== Completed ===\n");
    printf("Threads: %d, Elements: %d\n", NUM_TEAMS * threads_per_team, array_size);
    
    return 0;
}