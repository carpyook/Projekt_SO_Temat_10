#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>

#define SHM_KEY 123
#define SEM_KEY 321

#define MAX_BUFFER_SIZE 10 // K
#define MAX_WEIGHT_BELT 200 //M

typedef struct {
    char type;
    char weight;
    int volume;
    pid_t worker_id;
} Package;

typedef struct {
    Package buffer[MAX_BUFFER_SIZE];
    int head;
    int tail;
    int current_count;
    int current_weight;
} SharedBelt;

static void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

#endif