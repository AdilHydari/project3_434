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
#define DEFAULT_ARRAY_SIZE 50000  // Larger for signal testing
#define DEFAULT_THREADS_PER_TEAM 4

// Global state
int *main_array;
int array_size = DEFAULT_ARRAY_SIZE;
int threads_per_team = DEFAULT_THREADS_PER_TEAM;
int completion_order[NUM_TEAMS] = {-1, -1, -1, -1};
int completion_index = 0;
pthread_mutex_t completion_mutex = PTHREAD_MUTEX_INITIALIZER;

// Signal testing support
int signal_test_mode = 0;
int signals_received = 0;
pthread_mutex_t signal_mutex = PTHREAD_MUTEX_INITIALIZER;

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
int team_signals[NUM_TEAMS][3] = {
    {SIGINT, SIGABRT, SIGILL},      // Team 0
    {SIGCHLD, SIGSEGV, SIGFPE},     // Team 1  
    {SIGHUP, SIGTSTP, SIGINT},      // Team 2
    {SIGABRT, SIGFPE, SIGHUP}       // Team 3
};

// Function declarations
int partition(int arr[], int low, int high);
void quicksort(int arr[], int low, int high);
void signal_handler(int sig);
void* thread_sort_function(void* arg);
void setup_signal_handlers(void);
void setup_team_signals(int team_id);
void initialize_array(void);
void create_teams(void);
void print_status(void);

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
    
    if (team_id == -1) {
        printf("[SIGNAL %s] MAIN THREAD caught signal %d (%s)\n", 
               timestamp, sig, strsignal(sig));
    } else {
        printf("[SIGNAL %s] Team %d, Thread %d caught signal %d (%s)\n", 
               timestamp, team_id, thread_index, sig, strsignal(sig));
        
        // Check if this signal should be handled by this team
        int should_handle = 0;
        for (int i = 0; i < 3; i++) {
            if (team_signals[team_id][i] == sig) {
                should_handle = 1;
                break;
            }
        }
        
        if (should_handle) {
            printf("[SIGNAL %s] ✓ Signal %d handled correctly by Team %d\n", timestamp, sig, team_id);
        } else {
            printf("[SIGNAL %s] ⚠ Signal %d received by Team %d (not assigned)\n", timestamp, sig, team_id);
        }
    }
    
    // Track signals for testing
    pthread_mutex_lock(&signal_mutex);
    signals_received++;
    printf("[SIGNAL %s] Total signals received: %d\n", timestamp, signals_received);
    pthread_mutex_unlock(&signal_mutex);
    
    fflush(stdout);
}

// Quicksort implementation  
void quicksort(int arr[], int low, int high) {
    if (low < high) {
        int pi = partition(arr, low, high);
        quicksort(arr, low, pi - 1);
        quicksort(arr, pi + 1, high);
    }
}

int partition(int arr[], int low, int high) {
    int pivot = arr[high];
    int i = (low - 1);
    
    for (int j = low; j <= high - 1; j++) {
        if (arr[j] < pivot) {
            i++;
            int temp = arr[i];
            arr[i] = arr[j];
            arr[j] = temp;
        }
    }
    int temp = arr[i + 1];
    arr[i + 1] = arr[high];
    arr[high] = temp;
    return (i + 1);
}

void* thread_sort_function(void* arg) {
    team_data_t *team = (team_data_t*)arg;
    pthread_t self = pthread_self();
    
    printf("[THREAD] Team %d starting (subarray size: %d)\n", 
           team->team_id, team->subarray_size);
    
    setup_team_signals(team->team_id);
    
    if (signal_test_mode) {
        printf("[SIGNAL_TEST] Team %d waiting for signals\n", team->team_id);
        sleep(2);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &team->start_time);
    
    // Only the first thread in each team performs sorting
    if (pthread_equal(self, team->threads[0])) {
        printf("[SORT] Team %d starting quicksort\n", team->team_id);
        
        if (team->subarray == NULL) {
            printf("[ERROR] Team %d: Subarray is NULL!\n", team->team_id);
            return NULL;
        }
        
        quicksort(team->subarray, 0, team->subarray_size - 1);
        
        clock_gettime(CLOCK_MONOTONIC, &team->end_time);
        
        pthread_mutex_lock(&completion_mutex);
        if (completion_index < NUM_TEAMS) {
            completion_order[completion_index] = team->team_id;
            completion_index++;
            team->completed = 1;
            
            double elapsed = (team->end_time.tv_sec - team->start_time.tv_sec) + 
                            (team->end_time.tv_nsec - team->start_time.tv_nsec) / 1e9;
            
            printf("[COMPLETED] Team %d finished in %.6f seconds\n", team->team_id, elapsed);
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
    
    while (!team->completed) {
        usleep(1000);
    }
    
    if (signal_test_mode) {
        printf("[SIGNAL_TEST] Team %d staying alive for signals\n", team->team_id);
        sleep(15);
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

void setup_team_signals(int team_id) {
    sigset_t block_set, unblock_set;
    sigemptyset(&block_set);
    sigemptyset(&unblock_set);
    
    int signals_blocked = 0;
    
    // Block signals handled by other teams
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
    
    for (int i = 0; i < 3; i++) {
        sigaddset(&unblock_set, team_signals[team_id][i]);
    }
    
    if (pthread_sigmask(SIG_BLOCK, &block_set, NULL) != 0) {
        printf("[ERROR] Team %d: Failed to block signals: %s\n", team_id, strerror(errno));
    } else {
        printf("[SETUP] Team %d: Blocked %d signals from other teams\n", 
               team_id, signals_blocked);
    }
    
    if (pthread_sigmask(SIG_UNBLOCK, &unblock_set, NULL) != 0) {
        printf("[ERROR] Team %d: Failed to unblock team signals: %s\n", team_id, strerror(errno));
    } else {
        printf("[SETUP] Team %d: Unblocked team signals %d, %d, %d\n", 
               team_id, team_signals[team_id][0], team_signals[team_id][1], team_signals[team_id][2]);
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
    
    printf("[INIT] Creating %d teams with %d threads each\n", NUM_TEAMS, threads_per_team);
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        teams[i].team_id = i;
        teams[i].num_threads = threads_per_team;
        teams[i].subarray_size = subarray_size;
        teams[i].start_index = i * subarray_size;
        teams[i].completed = 0;
        
        teams[i].subarray = malloc(subarray_size * sizeof(int));
        memcpy(teams[i].subarray, &main_array[teams[i].start_index], 
               subarray_size * sizeof(int));
        
        teams[i].threads = malloc(threads_per_team * sizeof(pthread_t));
        
        printf("[INIT] Team %d handles signals [%d, %d, %d]\n", 
               i, team_signals[i][0], team_signals[i][1], team_signals[i][2]);
    }
}

void print_status() {
    printf("\n=== SIGNAL TESTING VERSION ===\n");
    printf("Array: %d elements\n", array_size);
    printf("Teams: %d\n", NUM_TEAMS);
    printf("Threads per team: %d\n", threads_per_team);
    printf("Signal test mode: %s\n", signal_test_mode ? "ENABLED" : "DISABLED");
    
    printf("\nSignal assignments:\n");
    for (int i = 0; i < NUM_TEAMS; i++) {
        printf("  Team %d: %d(%s), %d(%s), %d(%s)\n", 
               i, 
               team_signals[i][0], strsignal(team_signals[i][0]),
               team_signals[i][1], strsignal(team_signals[i][1]),
               team_signals[i][2], strsignal(team_signals[i][2]));
    }
    printf("==============================\n\n");
}

int main(int argc, char *argv[]) {
    printf("=== ECE 434 Project 1: Signal Testing Version ===\n");
    printf("Process PID: %d\n", getpid());
    
    // Parse arguments
    if (argc > 1) array_size = atoi(argv[1]);
    if (argc > 2) threads_per_team = atoi(argv[2]);
    if (argc > 3) signal_test_mode = atoi(argv[3]);
    
    // Block all signals in main thread
    sigset_t block_all, old_mask;
    sigfillset(&block_all);
    pthread_sigmask(SIG_BLOCK, &block_all, &old_mask);
    
    initialize_array();
    create_teams();
    print_status();
    setup_signal_handlers();
    
    printf("[STARTING] Creating teams...\n");
    
    for (int i = 0; i < NUM_TEAMS; i++) {
        for (int j = 0; j < teams[i].num_threads; j++) {
            pthread_create(&teams[i].threads[j], NULL, 
                          thread_sort_function, &teams[i]);
        }
        usleep(100000);
    }
    
    if (signal_test_mode) {
        printf("\n🚨 SIGNAL TEST MODE ACTIVE 🚨\n");
        printf("Process PID: %d\n", getpid());
        printf("Send signals using:\n");
        printf("  kill -INT %d   (Team 0,2)\n", getpid());
        printf("  kill -HUP %d   (Team 2,3)\n", getpid());
        printf("  kill -ABRT %d  (Team 0,3)\n", getpid());
        printf("Waiting 10 seconds for signals...\n");
        
        for (int i = 0; i < 10; i++) {
            sleep(1);
            pthread_mutex_lock(&signal_mutex);
            int current_signals = signals_received;
            pthread_mutex_unlock(&signal_mutex);
            
            if (current_signals > 0) {
                printf("⏰ %d seconds: %d signals received\n", i+1, current_signals);
            } else {
                printf("⏰ %d seconds: waiting...\n", i+1);
            }
        }
        
        printf("Signal testing period completed.\n");
    }
    
    // Wait for completion
    for (int i = 0; i < NUM_TEAMS; i++) {
        for (int j = 0; j < teams[i].num_threads; j++) {
            pthread_join(teams[i].threads[j], NULL);
        }
    }
    
    printf("\n=== RESULTS ===\n");
    pthread_mutex_lock(&signal_mutex);
    printf("Total signals received: %d\n", signals_received);
    pthread_mutex_unlock(&signal_mutex);
    
    printf("Team completion order:\n");
    for (int i = 0; i < NUM_TEAMS; i++) {
        if (completion_order[i] != -1) {
            printf("  %d: Team %d\n", i + 1, completion_order[i]);
        }
    }
    
    // Cleanup
    for (int i = 0; i < NUM_TEAMS; i++) {
        free(teams[i].subarray);
        free(teams[i].threads);
    }
    free(main_array);
    
    printf("\n=== Signal Testing Completed ===\n");
    return 0;
}