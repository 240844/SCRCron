#include <stdio.h>
#include <spawn.h>
#include "cron.h"
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <iso646.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include "logger.h"
#include <vector>

using namespace std;

static pthread_mutex_t query_mutex;
vector<scheduled_query_t*> qvec;

int start_server(int server_id);
void connect_to_server(data_t* shared_data, int argc, char* argv[]);
void fill_query(scheduled_query_t* query);
void list_query(scheduled_query_t* query);
void timer(union sigval query_sig);
void delete_cron_query(unsigned long id);


int main(int argc, char* argv[]) {
    const int server_id = shm_open(SHM_SERVER, O_RDONLY, 0777);
    data_t* shared_data = (data_t*) mmap(NULL, sizeof(data_t), PROT_READ, MAP_SHARED, server_id, 0);
    if (!shared_data) {
        if (server_id != 1) munmap(shared_data, sizeof(data_t));
        start_server(server_id);
    }
    else {
        connect_to_server(shared_data, argc, argv);
        close(server_id);
        munmap(shared_data, sizeof(data_t));
    }
    printf("Hello, World!\n");
    return 0;
}


//all functions used for starting server
int start_server(int server_id) {
    if (server_id != -1) {
        close(server_id);
        shm_unlink(SHM_SERVER);
    }
    server_id = shm_open(SHM_SERVER, O_RDWR | O_CREAT, 0777);

    if (server_id == -1) {
        printf("Couldn't open shared memory");
        return -1;
    }

    ftruncate(server_id, sizeof(data_t));
    data_t* shared_data = (data_t*) mmap(NULL, sizeof(data_t), PROT_READ | PROT_WRITE,
        MAP_SHARED, server_id, 0);

    if (!shared_data) {
        close(server_id);
        shm_unlink(SHM_SERVER);
        printf("Couldn't map shared memory");
        return -1;
    }

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_curmsgs = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(scheduled_query_t);

    const mqd_t server_queue = mq_open(TASK_QUEUE, O_RDONLY | O_CREAT, 0666, &attr);

    if (server_queue == -1) {
        munmap(shared_data, sizeof(data_t));
        close(server_id);
        shm_unlink(SHM_SERVER);
        printf("Failed to create queue");
        return -1;
    }

    pthread_mutex_init(&query_mutex, NULL);

    shared_data->pid = getpid();

    printf("\nServer started\n");
    printf("PID: %d\n", getpid());
    init_logger();

    bool is_server_running = true;
    unsigned long counter = 0;

    while (is_server_running) {
        scheduled_query_t query = {0};

        mq_receive(server_queue, (char*)&query, sizeof(scheduled_query_t), NULL);

        query_operation_t operation = query.operation;

        switch (operation) {
            case QUERY_OP_EXIT:
                save_log(LOG_STANDARD, "Exit query\n");
                is_server_running = false;
                break;
            case QUERY_OP_ADD:
                save_log(LOG_STANDARD, "Add query\n");
                fill_query(&query);
                query.id = counter;
                counter++;
                pthread_mutex_lock(&query_mutex);
                qvec.push_back(&query);
                pthread_mutex_unlock(&query_mutex);
                save_log(LOG_STANDARD, "Added task to query\n");
                break;
            case QUERY_OP_SHOW:
                save_log(LOG_STANDARD, "List query\n");
                pthread_mutex_lock(&query_mutex);
                list_query(&query);
                pthread_mutex_unlock(&query_mutex);
                break;
            case QUERY_OP_DELETE:
                save_log(LOG_STANDARD, "Delete query\n");
                pthread_mutex_lock(&query_mutex);
                delete_cron_query(query.id);
                pthread_mutex_unlock(&query_mutex);
                break;
        }

    }

    pthread_mutex_destroy(&query_mutex);
    mq_close(server_queue);
    close(server_id);

    destroy_logger();
    mq_unlink(TASK_QUEUE);
    shm_unlink(SHM_SERVER);
    munmap(shared_data, sizeof(data_t));
    return server_id;
}


void list_query(scheduled_query_t* query) {
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_curmsgs = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(scheduled_query_t);
    int queue = mq_open(query->queue_name, O_WRONLY, 0666, &attr);
    for (int i=0; i<qvec.size();i++) {
        scheduled_query_t* q = qvec.at(i);
        q->should_terminate = false;
        mq_send(queue, (char*) q, sizeof(scheduled_query_t), 1);
    }

    scheduled_query_t q;
    q.should_terminate = true;
    mq_send(queue, (char*) &q, sizeof(scheduled_query_t), 1);
    mq_close(queue);
}


void fill_query(scheduled_query_t* query) {
    timer_t timer_id;

    struct sigevent timer_sigvent = {0};

    timer_sigvent.sigev_notify = SIGEV_THREAD;
    timer_sigvent.sigev_notify_function = timer;
    timer_sigvent.sigev_value.sival_ptr = query;

    timer_create(CLOCK_REALTIME, &timer_sigvent, &timer_id);

    struct itimerspec timer_itimerspec = {0};

    timer_itimerspec.it_value.tv_sec = query->execution_time;
    timer_itimerspec.it_value.tv_nsec = 0;
    timer_itimerspec.it_interval.tv_sec = query->repeat_interval;
    timer_itimerspec.it_interval.tv_nsec = 0;
    int is_abs = 0;
    if (query->absolute_time) {
        is_abs = TIMER_ABSTIME;
    }
    timer_settime(timer_id, is_abs, &timer_itimerspec, NULL);
    query->timer_id = timer_id;

}


void delete_cron_query(const unsigned long id) {
    for (int i=0; i<qvec.size(); i++) {
        if (qvec.at(i)->id == id) {
            timer_delete(qvec.at(i)->timer_id);
            qvec.erase(qvec.begin() + i);
            break;
        }
    }
}


void timer(const union sigval query_sig) {
    pthread_mutex_lock(&query_mutex);
    scheduled_query_t* query = (scheduled_query_t*)query_sig.sival_ptr;
    pid_t pid;
    char* argv[3];
    argv[0] = query->command_name;
    argv[1] = query->args;
    argv[2] = NULL;

    posix_spawn(&pid, query->command_name, NULL, NULL, argv, environ);
    pthread_mutex_unlock(&query_mutex);

    if (query->repeat_interval == 0) {
        delete_cron_query(query->id);
    }
}


//client functions
void connect_to_server(data_t* shared_data, int argc, char* argv[]) {
    if (argc <= 1) {
        printf("Not enough arguments\n");
        return;
    }
    if (shared_data == NULL) {
        errno = EINVAL;
        printf("Couldn't open mapped shared memory");
        return;
    }
    if (kill(shared_data->pid, 0) != 0) { // check if running
        errno = EINVAL;
        printf("Couldn't access server pid");
        return;
    }

    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_curmsgs = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof(scheduled_query_t);
    mqd_t queue_write = mq_open(TASK_QUEUE, O_WRONLY, 0666, &attr);

    if (queue_write == -1) {
        printf("Couldn't access queue");
        return;
    }

    scheduled_query_t query = {0};
    if (strcmp(argv[1], "add") == 0) {
        query.operation = QUERY_OP_ADD;

        if (strcmp(argv[2], "rel") == 0) {
            if (argc < 4) {
                sscanf(argv[3], "%ld", &query.execution_time);

                int i = 4;
                if (strcmp(argv[i], "-req") == 0) {
                    sscanf(argv[i+1], "%ld", &query.repeat_interval);
                    i += 2;
                }
                strcat(query.command_name, argv[i]);
                i++;
                for (int j=i;j<argc; j++) {
                    if (j!=i) strcat(query.args, " ");
                    strcat(query.args, argv[j]);
                }
                query.absolute_time = false;
                mq_send(queue_write, (char*)&query, sizeof(scheduled_query_t), 0);
            }
        }
        else if (strcmp(argv[2], "abs") == 0) {
            if (argc >= 5) {
                struct tm abs_time;
                sscanf(argv[3], "%d%*c%d%*c%d", &abs_time.tm_hour, &abs_time.tm_min, &abs_time.tm_sec);
                sscanf(argv[3], "%d%*c%d%*c%d", &abs_time.tm_mday, &abs_time.tm_mon, &abs_time.tm_year);

                abs_time.tm_mon -= 1;
                abs_time.tm_year -= 1900;

                int i = 5;
                if (strcmp(argv[i], "-req") == 0) {
                    sscanf(argv[i+1], "%ld", &query.repeat_interval);
                    i += 2;
                }
                strcat(query.command_name, argv[i]);
                i++;
                for (int j=i;j<argc; j++) {
                    if (j!=i) strcat(query.args, " ");
                    strcat(query.args, argv[j]);
                }

                query.execution_time = mktime(&abs_time);
                query.absolute_time = true;

                mq_send(queue_write, (char*)&query, sizeof(scheduled_query_t), 0);
            }
        }
    }
    else if (strcmp(argv[1], "del") == 0){
        query.operation = QUERY_OP_DELETE;
        sscanf(argv[2], "%ld", &query.id);
        mq_send(queue_write, (char*)&query, sizeof(scheduled_query_t), 0);
    }
    else if (strcmp(argv[1], "list") == 0) {
        query.operation = QUERY_OP_SHOW;

        sprintf(query.queue_name, "TASK_QUEUE_%d", getpid());

        int queue_read = mq_open(query.queue_name, O_RDONLY | O_CREAT, 0666, &attr);

        if (queue_read == -1) {
            printf("failed to open queue");
            mq_unlink(query.queue_name);
            mq_close(queue_write);
            return;
        }

        mq_send(queue_write, (char*)&query, sizeof(scheduled_query_t), 1);
        scheduled_query_t res;
        while (true) {
            mq_receive(queue_read, (char*)&res, sizeof(scheduled_query_t), NULL);
            if (res.should_terminate) break;
            printf("Query id:%ld\n", res.id);
            printf("command:%s\n", res.command_name);
            printf("arg:%s\n", res.args);
        }
        mq_close(queue_read);
        mq_unlink(query.queue_name);
    }
    else if (strcmp(argv[1], "close") == 0) {
        query.operation = QUERY_OP_EXIT;
        mq_send(queue_write, (char*)&query, sizeof(scheduled_query_t), 0);
    }
    mq_close(queue_write);

}
