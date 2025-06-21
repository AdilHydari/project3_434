#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h> 
#include <errno.h>

int main(int argc, char *argv[]) {
    printf("Signal Tester - ECE 434 Project 1\n");
    
    if (argc != 3) {
        printf("Usage: %s <pid> <signal_number>\n", argv[0]);
        printf("\nSignals:\n");
        printf("  %d - SIGINT\n", SIGINT);
        printf("  %d - SIGABRT\n", SIGABRT);
        printf("  %d - SIGILL\n", SIGILL);
        printf("  %d - SIGCHLD\n", SIGCHLD);
        printf("  %d - SIGSEGV\n", SIGSEGV);
        printf("  %d - SIGFPE\n", SIGFPE);
        printf("  %d - SIGHUP\n", SIGHUP);
        printf("  %d - SIGTSTP\n", SIGTSTP);
        printf("\nExample: %s 1234 2\n", argv[0]);
        return 1;
    }
    
    pid_t target_pid = atoi(argv[1]);
    int signal_num = atoi(argv[2]);
    
    if (target_pid <= 0) {
        printf("Error: Invalid PID %d\n", target_pid);
        return 1;
    }
    
    if (signal_num < 1 || signal_num > 31) {
        printf("Error: Invalid signal %d (must be 1-31)\n", signal_num);
        return 1;
    }
    
    printf("Sending signal %d to process %d\n", signal_num, target_pid);
    
    if (kill(target_pid, signal_num) == -1) {
        perror("Failed to send signal");
        return 1;
    }
    
    printf("Signal sent successfully\n");
    return 0;
}