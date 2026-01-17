#define _XOPEN_SOURCE 700
#include "common.h"
#include <time.h>
#include <signal.h>

volatile sig_atomic_t express_order = 0;

void handle_sigusr2(int sig) {
    (void)sig;
    express_order = 1;
}

int main() {
    printf("[P4] Pracownik Ekspresowy gotowy (PID: %d).\n", getpid());
    
    struct sigaction sa;
    sa.sa_handler = handle_sigusr2;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR2, &sa, NULL);

    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), 0);
    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    int sem_id = semget(SEM_KEY, 4, 0);
    srand(time(NULL) ^ getpid());

    while(1) {
        pause(); // czekaj na sygnal

        if (express_order) {
            printf("[P4] Otrzymalem sygnal! Przygotowuje ekspres.\n");
            
            Package pkg;
            pkg.type = 'E'; // ekspres
            pkg.volume = (64.0*38.0*41.0)/1000000.0; // gabaryt C
            pkg.weight = 1.0 + (rand()%240)/10.0; // 1-25kg
            pkg.worker_id = getpid();

            // blokada pamieci na chwile
            if (sem_wait_wrapper(sem_id, SEM_MUTEX) == -1) continue;
            
            belt->express_pkg = pkg;
            belt->express_ready = 1; // ustawiamy dla ciezarowki
            
            printf("[P4] Paczka ekspresowa (%.1f kg) gotowa!\n", pkg.weight);
            
            sem_signal(sem_id, SEM_MUTEX);
            
            // obudzenie ciezarowki
            sem_signal(sem_id, SEM_FULL);
            
            express_order = 0;
        }
    }
    return 0;
}