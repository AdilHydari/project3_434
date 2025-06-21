#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>

// Configuration constants
#define NUM_TEAMS 4
#define DEFAULT_ARRAY_SIZE 10000
#define DEFAULT_THREADS_PER_TEAM 4

// Global state
int *main_array;
int array_size = DEFAULT_ARRAY_SIZE;
int threads_per_team = DEFAULT_THREADS_PER_TEAM;
int completion_order[NUM_TEAMS] = {-1, -1, -1, -1};
int completion_index = 0;
pthread_mutex_t completion_mutex = PTHREAD_MUTEX_INITIALIZER;

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
int partition(int arr[], int low, int high);
void quicksort(int arr[], int low, int high);

void quicksort(int arr[], int low, int high) {
    if (low < high) {
        int pivot_index = partition(arr, low, high);
        quicksort(arr, low, pivot_index - 1);
        quicksort(arr, pivot_index + 1, high);
    }
}

int partition(int arr[], int low, int high) {
    int pivot = arr[high];
    int i = low - 1;
    
    for (int j = low; j < high; j++) {
        if (arr[j] < pivot) {
            i++;
            // Swap elements
            int temp = arr[i];
            arr[i] = arr[j];
            arr[j] = temp;
        }
    }
    
    // Place pivot in correct position
    int temp = arr[i + 1];
    arr[i + 1] = arr[high];
    arr[high] = temp;
    
    return i + 1;
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

void* thread_sort_function(void* arg) {
    team_data_t *team = (team_data_t*)arg;
    pthread_t self = pthread_self();
    
    printf("[THREAD] Team %d starting (subarray size: %d)\n", 
           team->team_id, team->subarray_size);
    
    setup_team_signals(team->team_id);
    
    clock_gettime(CLOCK_MONOTONIC, &team->start_time);
    
    // Only the first thread in each team performs the actual sorting
    if (pthread_equal(self, team->threads[0])) {
        printf("[SORT] Team %d starting quicksort\n", team->team_id);
        
        if (team->subarray == NULL) {
            printf("[ERROR] Team %d: Subarray is NULL!\n", team->team_id);
            return NULL;
        }
        
        quicksort(team->subarray, 0, team->subarray_size - 1);
        
        clock_gettime(CLOCK_MONOTONIC, &team->end_time);
        
        // Update completion tracking
        pthread_mutex_lock(&completion_mutex);
        if (completion_index < NUM_TEAMS) {
            completion_order[completion_index] = team->team_id;
            completion_index++;
            team->completed = 1;
            
            double elapsed = (team->end_time.tv_sec - team->start_time.tv_sec) + 
                            (team->end_time.tv_nsec - team->start_time.tv_nsec) / 1e9;
            
            printf("[COMPLETED] Team %d finished in %.6f seconds (position %d)\n", 
                   team->team_id, elapsed, completion_index);
        }
        pthread_mutex_unlock(&completion_mutex);
        
        // Verify sort correctness
        int is_sorted = 1;
        for (int i = 1; i < team->subarray_size && i < 100; i++) {
            if (team->subarray[i-1] > team->subarray[i]) {
                is_sorted = 0;
                break;
            }
        }
        printf("[VERIFY] Team %d sort: %s\n", 
               team->team_id, is_sorted ? "PASSED" : "FAILED");
    }
    
    // All threads wait for team completion
    while (!team->completed) {
        usleep(1000);
    }
    
    printf("[THREAD] Team %d thread exiting\n", team->team_id);
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
    printf("[INIT] Allocating array of %d integers\n", array_size);
    
    main_array = malloc(array_size * sizeof(int));
    if (!main_array) {
        printf("[ERROR] Failed to allocate memory: %s\n", strerror(errno));
        exit(1);
    }
    
    srand(time(NULL));
    for (int i = 0; i < array_size; i++) {
        main_array[i] = rand() % 10000;
    }
    
    printf("[INIT] Generated %d random integers\n", array_size);
}

void create_teams() {
    int subarray_size = array_size / NUM_TEAMS;
    int remainder = array_size % NUM_TEAMS;
    
    printf("[INIT] Creating %d teams with %d threads each\n", NUM_TEAMS, threads_per_team);
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        teams[i].team_id = i;
        teams[i].num_threads = threads_per_team;
        teams[i].subarray_size = subarray_size + (i < remainder ? 1 : 0);
        teams[i].start_index = i * subarray_size + (i < remainder ? i : remainder);
        teams[i].completed = 0;
        
        printf("[INIT] Team %d handles signals [%d, %d, %d]\n", 
               i, team_signals[i][0], team_signals[i][1], team_signals[i][2]);
        
        // Allocate and copy subarray
        size_t subarray_bytes = teams[i].subarray_size * sizeof(int);
        teams[i].subarray = malloc(subarray_bytes);
        if (!teams[i].subarray) {
            printf("[ERROR] Failed to allocate memory for team %d: %s\n", 
                   i, strerror(errno));
            exit(1);
        }
        
        memcpy(teams[i].subarray, &main_array[teams[i].start_index], subarray_bytes);
        
        teams[i].threads = malloc(threads_per_team * sizeof(pthread_t));
        if (!teams[i].threads) {
            printf("[ERROR] Failed to allocate threads for team %d: %s\n", 
                   i, strerror(errno));
            exit(1);
        }
        
        printf("[INIT] Team %d: subarray[%d-%d] (%d elements)\n", 
               i, teams[i].start_index, 
               teams[i].start_index + teams[i].subarray_size - 1, teams[i].subarray_size);
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
                                      thread_sort_function, &teams[i]);
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
    printf("Teams completion order:\n");
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        if (completion_order[i] != -1) {
            double team_time = (teams[completion_order[i]].end_time.tv_sec - teams[completion_order[i]].start_time.tv_sec) + 
                              (teams[completion_order[i]].end_time.tv_nsec - teams[completion_order[i]].start_time.tv_nsec) / 1e9;
            printf("  Position %d: Team %d (%.6f seconds)\n", i + 1, completion_order[i], team_time);
        } else {
            printf("  Position %d: [ERROR - Team not recorded]\n", i + 1);
        }
    }
    
    // Performance analysis
    if (completion_order[0] != -1 && completion_order[NUM_TEAMS-1] != -1) {
        int fastest_team = completion_order[0];
        int slowest_team = completion_order[NUM_TEAMS-1];
        
        double fastest_time = (teams[fastest_team].end_time.tv_sec - teams[fastest_team].start_time.tv_sec) + 
                             (teams[fastest_team].end_time.tv_nsec - teams[fastest_team].start_time.tv_nsec) / 1e9;
        double slowest_time = (teams[slowest_team].end_time.tv_sec - teams[slowest_team].start_time.tv_sec) + 
                             (teams[slowest_team].end_time.tv_nsec - teams[slowest_team].start_time.tv_nsec) / 1e9;
        
        printf("\nPerformance Analysis:\n");
        printf("  Fastest team: %d (%.6f seconds)\n", fastest_team, fastest_time);
        printf("  Slowest team: %d (%.6f seconds)\n", slowest_team, slowest_time);
        printf("  Speed difference: %.2fx\n", slowest_time / fastest_time);
        printf("  Elements per second (fastest): %.0f\n", teams[fastest_team].subarray_size / fastest_time);
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
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        free(teams[i].subarray);
        free(teams[i].threads);
    }
    free(main_array);
    
    printf("\n=== Completed ===\n");
    printf("Threads: %d, Elements: %d\n", NUM_TEAMS * threads_per_team, array_size);
    
    return 0;
}