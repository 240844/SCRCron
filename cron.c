#include <stdio.h>
#include <spawn.h>
#include "cron.h"
#include <time.h>
#include <errno.h>
#include <iso646.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>

int start_server(int server_id);
void connect_to_server(data_t* shared_data, int argc, char* argv[]);

int main(int argc, char* argv[]) {
    int server_id = shm_open(SHM_SERVER, O_RDONLY, 0777);
    data_t* shared_data = mmap(NULL, sizeof(data_t), PROT_READ, MAP_SHARED, server_id, 0);
    if (!shared_data) {
        munmap(shared_data, sizeof(data_t));
        close(server_id);
        shm_unlink(SHM_SERVER);

        start_server(server_id);
    }
    else {
        connect_to_server(shared_data, argc, argv);
    }
    printf("Hello, World!\n");
    return 0;
}

int start_server(int server_id) {
    if (server_id == -1) {
        errno = EINVAL;
        printf("Couldn't open shared memory");
        return -1;
    }

    int server_id = shm_open(SHM_SERVER, O_RDWR | O_CREAT, 0777);

    if (server_id == -1) {
        printf("Couldn't open shared memory");
        return -1;
    }

    ftruncate(server_id, sizeof(data_t));
    data_t* shared_data = mmap(NULL, sizeof(data_t), PROT_READ | PROT_WRITE,
        MAP_SHARED, server_id, 0);

    if (!shared_data) {
        printf("Couldn't map shared memory");
        return -1;
    }

    struct mq_attr attr = {0};

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
