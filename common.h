#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/msg.h>
#include <string.h>
#include <time.h>

#define SHM_KEY 123
#define SEM_KEY 321
#define MSG_KEY 456

#define MAX_BUFFER_SIZE 10 // K
#define MAX_WEIGHT_BELT 200.0 //M
#define MSG_TYPE_LOG 1
#define MSG_MAX_TEXT 256
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

    Package express_pkg; 
    int express_ready; // 1 = paczka gotowa do odbioru, 0 = brak
    volatile int shutdown;
} SharedBelt;

// struktura wiadomosci dla kolejki komunikatow
typedef struct {
    long mtype;
    char text[MSG_MAX_TEXT];
    pid_t sender_pid;
    time_t timestamp;
} LogMessage;

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
    SEM_FULL  = 2,  // Liczba zajetych miejsc (paczek)
    SEM_RAMP  = 3 // Kolejka do rampy
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

// funkcja zwracajaca -1 jak przerwie ja sygnal, 0 jak sukces
static inline int sem_wait_wrapper(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) return -1; // przerwane przez sygnal
        perror("Blad sem_wait_wrapper");
        exit(EXIT_FAILURE);
    }
    return 0;
}

// Sprawdzanie limitu procesow w systemie
static inline int check_process_limit(int needed) {
    struct rlimit rl;
    
    if (getrlimit(RLIMIT_NPROC, &rl) == -1) {
        perror("getrlimit RLIMIT_NPROC");
        return -1;
    }
    
    printf("[INFO] Limit procesow: soft=%ld, hard=%ld, potrzeba=%d\n", 
           (long)rl.rlim_cur, (long)rl.rlim_max, needed);
    
    if (rl.rlim_cur != RLIM_INFINITY && (long)rl.rlim_cur < needed + 10) {
        fprintf(stderr, "[BLAD] Limit procesow moze byc niewystarczajacy!\n");
        return -1;
    }
    
    return 0;
}

// sprawdzemie czy kolejka nie jest pelna
static inline int send_log_message(int msg_id, const char *text, pid_t sender) {
    LogMessage msg;
    msg.mtype = MSG_TYPE_LOG;
    msg.sender_pid = sender;
    msg.timestamp = time(NULL);
    strncpy(msg.text, text, MSG_MAX_TEXT - 1);
    msg.text[MSG_MAX_TEXT - 1] = '\0';

    size_t msg_size = sizeof(LogMessage) - sizeof(long);

    if (msgsnd(msg_id, &msg, msg_size, IPC_NOWAIT) == -1) {
        if (errno == EAGAIN) {
            fprintf(stderr, "[WARN] Kolejka komunikatow pelna! \n");
            return -1;
        }
        perror("msgsnd");
        return -1;
    }

    return 0;
}

static inline int receive_log_message(int msg_id, LogMessage *msg) {
    size_t msg_size = sizeof(LogMessage) - sizeof(long);

    if (msgrcv(msg_id, msg, msg_size, MSG_TYPE_LOG, IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) return 0;
        perror("msgrcv");
        return -1;
    }

    return 1;
}

#endif