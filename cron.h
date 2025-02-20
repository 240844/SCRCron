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
    unsigned long id;
    char command[256];
    char command_args[256];

    bool is_absolute;
    time_t time_value;
    time_t time_interval;
    operation_t operation;
    timer_t timer_id;

    char queue_name[50];
    bool terminate;
}query_t;

void timer(union sigval query_sig);
void delete_cron_query(unsigned long id);

#endif //CRON_H
