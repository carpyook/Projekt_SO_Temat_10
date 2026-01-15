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
#define MAX_WEIGHT_BELT 200.0 //M

typedef struct {
    char type;
    float weight;
    float volume;
    pid_t worker_id;
} Package;

typedef struct {
    Package buffer[MAX_BUFFER_SIZE];
    int head;
    int tail;
    int current_count;
    float current_weight;
} SharedBelt;

static void check_error(int ret, const char *msg) {
    if (ret == -1) {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

// union wymagany przez funkcje semctl
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

//nazwy semaforow
enum SemType {
    SEM_MUTEX = 0, // Dostep do pamieci (binarny 0/1)
    SEM_EMPTY = 1, // Liczba wolnych miejsc
    SEM_FULL  = 2  // Liczba zajetych miejsc (paczek)
};

//funkcja P
static void sem_wait(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    if (semop(sem_id, &op, 1) == -1) {
        perror("Blad sem_wait");
        exit(EXIT_FAILURE);
    }
}

//funkcja V
static void sem_signal(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1; // Dodaj 1
    op.sem_flg = 0;
    if (semop(sem_id, &op, 1) == -1) {
        perror("Blad sem_signal");
        exit(EXIT_FAILURE);
    }
}

#endif