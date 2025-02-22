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
    QUERY_OP_ADD,
    QUERY_OP_DELETE,
    QUERY_OP_SHOW,
    QUERY_OP_EXIT
}query_operation_t;

typedef struct {
    unsigned long id;
    char command_name[256];
    char args[256];

    bool absolute_time;
    time_t execution_time;
    time_t repeat_interval;
    query_operation_t operation;
    timer_t timer_id;

    char queue_name[50];
    bool should_terminate;
}scheduled_query_t;

#endif //CRON_H
