#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include <string.h>
#include <errno.h>
#include "logger.h"
#include <semaphore.h>
#include <stdbool.h>
#include <iso646.h>

static sig_atomic_t is_init = ATOMIC_VAR_INIT(false); // is logger initialized
volatile static sig_atomic_t is_logger_active =  ATOMIC_VAR_INIT(true);
volatile static sig_atomic_t current_logger_detail = ATOMIC_VAR_INIT(LOG_MIN);

static pthread_mutex_t log_mutex;

static pthread_t dumb_thread;
static sem_t dumb_sem;
static pthread_mutex_t dump_msg_mutex;
char* message_to_dump;

static pthread_mutex_t terminate_mutex;
static sig_atomic_t terminate_thread = ATOMIC_VAR_INIT(false);

void handle_dump(int signo, siginfo_t* info, void *other) {
    sem_post(&dumb_sem);
}

void handle_change_logger_active(int signo, siginfo_t* info, void *other) {
    atomic_store(&is_logger_active, !atomic_load(&is_logger_active));
}

void handle_level(int signo, siginfo_t* info, void *other) {
    const int new_detail_level = info->si_value.sival_int;
    if (new_detail_level < LOG_MIN or new_detail_level > LOG_MAX) return;
    atomic_store(&current_logger_detail, new_detail_level);
}

void* dump_message(void* arg) {
    while (true) {
        if (sem_wait(&dumb_sem) != 0) {
            printf("Sem wait failure\n");
            continue;
        }
        pthread_mutex_lock(&terminate_mutex);
        if (atomic_load(&terminate_thread)) {
            pthread_mutex_unlock(&terminate_mutex);
            break;
        }
        pthread_mutex_unlock(&terminate_mutex);

        char filename[99];
        char date[50];
        time_t curr_time = time(NULL);
        strftime(date, sizeof(date), "%Y-%m-%d_%H:%M:%S", gmtime(&curr_time));
        sprintf(filename, "log_%s.log",date);

        FILE* fptr = fopen(filename, "w+");
        if (!fptr) {
            printf("Failed to open a file\n");
            continue;
        }

        pthread_mutex_lock(&dump_msg_mutex);
        if (!message_to_dump) {
            fprintf(fptr, "NO DUMP MESSAGE\n");
        }
        else {
            fprintf(fptr, "%s\n", message_to_dump);
            free(message_to_dump);
        }
        pthread_mutex_unlock(&dump_msg_mutex);
        fclose(fptr);
    }
}

void save_log(const logger_level_t level, const char* message) {
    pthread_mutex_lock(&log_mutex);

    if (atomic_load(&is_logger_active) == false or atomic_load(&is_init) == false) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    if (level < atomic_load(&current_logger_detail)) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    FILE *fptr = fopen(FILENAME, "a+");

    if (!fptr) {
        printf("Failed to open a file\n");
        return;
    }

    fprintf(fptr, "New log message:\n");
    fprintf(fptr, "%s\n",message);
    fclose(fptr);

    pthread_mutex_unlock(&log_mutex);
}

int set_message(char* message) {
    if (!message) {
        errno = EINVAL;
        return -1;
    }
    char* msg = calloc(sizeof(message), sizeof(char));
    if (!msg) {
        errno = ENOMEM;
        return -1;
    }

    pthread_mutex_lock(&dump_msg_mutex);
    if (message_to_dump) {
        free(message_to_dump);
    }
    message_to_dump = msg;
    pthread_mutex_unlock(&dump_msg_mutex);

    return 1;
}

void init_logger(void) {
    if (atomic_load(&is_init)) {
        return;
    }

    message_to_dump = NULL;

    sem_init(&dumb_sem, 0, 0);
    pthread_mutex_init(&log_mutex, NULL);
    pthread_mutex_init(&dump_msg_mutex, NULL);
    pthread_mutex_init(&terminate_mutex, NULL);

    struct sigaction action;

    sigfillset(&action.sa_mask);
    action.sa_sigaction = handle_dump;
    sigaction(SIGRTMIN, &action, NULL);

    sigfillset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = handle_change_logger_active;
    sigaction(SIGRTMIN+1, &action, NULL);

    sigfillset(&action.sa_mask);
    action.sa_sigaction = handle_level;
    sigaction(SIGRTMIN+2, &action, NULL);

    pthread_create(&dumb_thread, NULL, dump_message, NULL);

    atomic_store(&is_init, true);
}

void destroy_logger(void) {
    pthread_mutex_lock(&terminate_mutex);
    atomic_store(&terminate_thread, true);
    pthread_mutex_unlock(&terminate_mutex);
    sem_post(&dumb_sem);
    pthread_join(dumb_thread, NULL);
    sem_destroy(&dumb_sem);
    pthread_mutex_destroy(&log_mutex);
    pthread_mutex_destroy(&dump_msg_mutex);
    pthread_mutex_destroy(&terminate_mutex);

    if (message_to_dump) {
        free(message_to_dump);
    }

    atomic_store(&terminate_thread, false);
    atomic_store(&is_init, false);
    atomic_store(&current_logger_detail, LOG_MIN);
}