#define _XOPEN_SOURCE 700
#include "common.h"
#include <time.h>
#include <signal.h>

volatile sig_atomic_t express_order = 0;
volatile sig_atomic_t should_exit = 0;

void handle_sigusr2(int sig) {
    (void)sig;
    express_order = 1;
}

void handle_sigterm(int sig) {
    (void)sig;
    should_exit = 1;
}

int main() {
    srand(time(NULL) ^ getpid());
    printf(YELLOW "[P4] Pracownik Ekspresowy gotowy (PID: %d).\n" RESET, getpid());
    
    struct sigaction sa;
    sa.sa_handler = handle_sigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);

    int shm_id = shmget(get_shm_key(), sizeof(SharedBelt), 0);
    if (shm_id == -1) {
        perror("P4: shmget");
        return 1;
    }

    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) {
        perror("P4: shmat");
        return 1;
    }

    int sem_id = semget(get_sem_key(), NUM_SEMS, 0);
    if (sem_id == -1) {
        perror("P4: semget");
        shmdt(belt);  // ← CLEANUP!
        return 1;
    }

    // kolejka komunikatow
    int msg_id = msgget(get_msg_key(), 0);
    if (msg_id == -1) {
        perror("P4: msgget");
        // kontynuuj bez kolejki
    }

    while(!should_exit && !belt->shutdown) {
        pause(); // czekaj na sygnal

        if (should_exit || belt->shutdown) break;

        if (express_order) {
            printf(YELLOW "[P4] Otrzymalem sygnal! Przygotowuje ekspres.\n" RESET);
            
            Package pkg;
            pkg.type = 'E'; // ekspres
            pkg.volume = (64.0 * 38.0 * 41.0) / 1000000.0; // gabaryt C
            pkg.weight = 1.0 + (rand() % 240) / 10.0; // 1-25kg
            pkg.worker_id = getpid();

            // blokada pamieci na chwile
            if (sem_wait_wrapper(sem_id, SEM_MUTEX) == -1) {
                express_order = 0;
                continue;
            }
            
            belt->express_pkg = pkg;
            belt->express_ready = 1; // ustawiamy dla ciezarowki

            printf(YELLOW "[P4] Paczka ekspresowa (%.1f kg) gotowa!\n" RESET, pkg.weight);

            // logowanie do kolejki
            if (msg_id != -1) {
                char log_msg[MSG_MAX_TEXT];
                snprintf(log_msg, MSG_MAX_TEXT, "P4 przygotowal ekspres %.1f kg", pkg.weight);
                send_log_message(msg_id, sem_id, log_msg, getpid());
            }

            sem_signal(sem_id, SEM_MUTEX);
            
            // obudzenie ciezarowki
            sem_signal(sem_id, SEM_FULL);
            
            express_order = 0;
        }
    }

    printf(YELLOW "[P4] Koniec pracy.\n" RESET);
    shmdt(belt);
    return 0;
}