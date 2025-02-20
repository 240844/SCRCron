#ifndef CRON_H
#define CRON_H

#include <signal.h>
#include <mqueue.h>
#include <stdbool.h>
#include <semaphore.h>

#define TASK_QUEUE "/TASK_QUEUE"
#define SHM_SERVER "/SHM_SERVER"

typedef struct {
    pid_t pid;
} data_t;

typedef enum {
    QUERY_ADD,
    QUERY_DELETE,
    QUERY_LIST,
    QUERY_EXIT
}operation_t;

typedef struct {
    long id;
    char command[256];
    char command_args[256];

    bool is_absolute; // czy jest czas wzgledny czy nie
    time_t time;
    time_t interval;
    operation_t operation;
    time_t timer_id;

    char queue_name[50];
}query_t;

void timer_thread(union sigval query_union);
void delete_cron_query(long id);

#endif //CRON_H
