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
#include "logger.h"
#include <vector>

using namespace std;

static pthread_mutex_t query_mutex;
vector<query_t> qvec;

int start_server(int server_id);
void connect_to_server(data_t* shared_data, int argc, char* argv[]);
void query_add(query_t* query);


int main(int argc, char* argv[]) {
    const int server_id = shm_open(SHM_SERVER, O_RDONLY, 0777);
    data_t* shared_data = (data_t*) mmap(NULL, sizeof(data_t), PROT_READ, MAP_SHARED, server_id, 0);
    if (!shared_data) {
        if (server_id != 1) munmap(shared_data, sizeof(data_t));
        start_server(server_id);
    }
    else {
        connect_to_server(shared_data, argc, argv);
    }
    printf("Hello, World!\n");
    return 0;
}

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
    attr.mq_msgsize = sizeof(query_t);

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
        query_t* query = (query_t*) calloc(1, sizeof(query_t));

        if (!query) {
            printf("Couldn't allocate memory\n");
            continue;
        }

        mq_receive(server_queue, (char*)query, sizeof(query_t), NULL);

        operation_t operation = query->operation;

        switch (operation) {
            case QUERY_EXIT:
                save_log(LOG_STANDARD, "Exit query\n");
                is_server_running = false;
                break;
            case QUERY_ADD:
                save_log(LOG_STANDARD, "Add query\n");
                query_add(query);
                break;
            case QUERY_LIST:
                save_log(LOG_STANDARD, "List query\n");
                break;
            case QUERY_DELETE:
                save_log(LOG_STANDARD, "Delete query\n");
                break;
        }

        free(query);
    }

    pthread_mutex_destroy(&query_mutex);
    mq_close(server_queue);
    close(server_id);

    mq_unlink(TASK_QUEUE);
    shm_unlink(SHM_SERVER);

    return server_id;
}

void connect_to_server(data_t* shared_data, int argc, char* argv[]) {
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

}
