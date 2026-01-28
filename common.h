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
#include <fcntl.h>

// kolory do terminala
#define RESET   "\033[0m"
#define RED     "\033[1;31m"
#define GREEN   "\033[1;32m"
#define YELLOW  "\033[1;33m"
#define BLUE    "\033[1;34m"
#define MAGENTA "\033[1;35m"
#define CYAN    "\033[1;36m"

#define REPORT_FILE "raport_symulacji.txt"
#define MAX_BUFFER_SIZE 10 // K pojemosc tasmy
#define MAX_WEIGHT_BELT 200.0 //M maksymalny udzwig
#define MSG_TYPE_LOG 1 // typ wiadomosci
#define MSG_MAX_TEXT 256 // maksymalna dlugosc tekstu w logu
#define MAX_MSG_QUEUE 100 // maksymalna liczba wiadomosci w kolejce

// funkcje generujace klucze IPC przy uzyciu ftok()
static inline key_t get_shm_key(void) {
    key_t key = ftok(".", 'S'); //dla pamieci dzielonej
    if (key == -1) {
        perror("ftok shm");
        exit(EXIT_FAILURE);
    }
    return key;
}

static inline key_t get_sem_key(void) {
    key_t key = ftok(".", 'M'); // dla mutex/semafory
    if (key == -1) {
        perror("ftok sem");
        exit(EXIT_FAILURE);
    }
    return key;
}

static inline key_t get_msg_key(void) {
    key_t key = ftok(".", 'Q'); // dla kolejki
    if (key == -1) {
        perror("ftok msg");
        exit(EXIT_FAILURE);
    }
    return key;
}

typedef struct {
    char type; // A B C lub E
    float weight; // waga w kg
    float volume; // objetosc w m3
    pid_t worker_id; // ID procesu ktory stworzyl paczke do raportu
} Package;

//glowna struktura przechowywana w pamieci wspoldzielonej, dostep maja wszystkie procesy
typedef struct {
    Package buffer[MAX_BUFFER_SIZE];
    int head; //zdejmowanie
    int tail; // wkladanie
    int current_count; //aktualna liczba paczek na tasmie
    float current_weight; // aktaulna calkowita waga na tasmie

    Package express_pkg; //kanal dla przesylki ekspres P4 -> Truck
    int express_ready; // 1 = paczka gotowa do odbioru, 0 = brak
    volatile int shutdown; // zakonczenie pracy
    int total_packages; // statystyki
    int total_trucks_sent;
} SharedBelt;

// struktura wiadomosci dla kolejki komunikatow
typedef struct {
    long mtype;
    char text[MSG_MAX_TEXT];
    pid_t sender_pid;
    time_t timestamp;
} LogMessage;

//f unkcje pomocnicze 

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
    SEM_EMPTY = 1, // Liczba wolnych miejsc na tasmie (dla workera)
    SEM_FULL  = 2,  // Liczba zajetych miejsc (paczek)
    SEM_RAMP  = 3, // Kolejka do rampy, tylko 1 ciezarowka laduje
    SEM_REPORT = 4, // Zapis do pliku
    SEM_MSG_GUARD = 5 // Straznik kolejki komunikatow, ogranicza ilosc wiad w kolejce
};

#define NUM_SEMS 6
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
        fprintf(stderr, "[BLAD] Limit procesow moze byc niewystarczajacy!\n"); // ostrzezenie jesli uzytkownik ma na maly limit procesow
        return -1;
    }
    
    return 0;
}
// Obsluga kolejki komunikatow
// bezpieczne wysylanie logu do loggera
// zapobiega przepelnieniu kolejki -> zwraca -1
static inline int send_log_message(int msg_id, int sem_id, const char *text, pid_t sender) {
    // pobranie klucza od straznika 
    struct sembuf guard_down;
    guard_down.sem_num = SEM_MSG_GUARD;
    guard_down.sem_op = -1;
    guard_down.sem_flg = IPC_NOWAIT; // nie czekaj, jesli  brak miejsca

    if (semop(sem_id, &guard_down, 1) == -1) {
        if (errno == EAGAIN) {
            fprintf(stderr, "[WARN] Kolejka komunikatow pelna (semafor straznik)!\n");
            return -1;
        }
        perror("semop SEM_MSG_GUARD down");
        return -1;
    }
    // wyslanie wiadomosci
    LogMessage msg;
    msg.mtype = MSG_TYPE_LOG;
    msg.sender_pid = sender;
    msg.timestamp = time(NULL);
    strncpy(msg.text, text, MSG_MAX_TEXT - 1);
    msg.text[MSG_MAX_TEXT - 1] = '\0'; // zabezpieczenie nulla

    size_t msg_size = sizeof(LogMessage) - sizeof(long);

    if (msgsnd(msg_id, &msg, msg_size, 0) == -1) {  // 0 = blokujace wyslanie
        perror("msgsnd");
        // oddaj klucz straznika z powrotem w razie bledu
        struct sembuf guard_up;
        guard_up.sem_num = SEM_MSG_GUARD;
        guard_up.sem_op = 1;
        guard_up.sem_flg = 0;
        semop(sem_id, &guard_up, 1);
        return -1;
    }

    return 0;
}

// odbior wiadomosci z kolejki, zwraca klucz straznikowi
static inline int receive_log_message(int msg_id, int sem_id, LogMessage *msg) {
    size_t msg_size = sizeof(LogMessage) - sizeof(long);

    if (msgrcv(msg_id, msg, msg_size, MSG_TYPE_LOG, IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) return 0; // brak wiadomosci
        perror("msgrcv");
        return -1;
    }

    // odebrano wiadomosc, zwroc klucz straznikowi
    struct sembuf guard_up;
    guard_up.sem_num = SEM_MSG_GUARD;
    guard_up.sem_op = 1;
    guard_up.sem_flg = 0;

    if (semop(sem_id, &guard_up, 1) == -1) {
        perror("semop SEM_MSG_GUARD up");
        return -1;
    }

    return 1; // sukces
}

#endif