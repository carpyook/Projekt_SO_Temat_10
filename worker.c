#define _POSIX_C_SOURCE 200809L // sigaction
#include "common.h"
#include <time.h>
#include <signal.h>

volatile sig_atomic_t should_exit = 0;

void handle_sigterm(int sig) {
    (void)sig;
    should_exit = 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uzycie: %s [TYP A/B/C]\n", argv[0]);
        return 1;

    }
    char type = argv[1][0]; // pobranie pierwszej litery
    if (type != 'A' && type != 'B' && type != 'C') {
        fprintf(stderr, "[BLAD] Nieprawidlowy typ paczki: %c\n", type);
        return 1;
    }

    srand(time(NULL) ^ getpid());

    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);


    printf("[P%c] pracownik uruchomiony, PID: %d\n", type, getpid());

    // pobranie zasobow

    // pamiec
    int shm_id = shmget(get_shm_key(), sizeof(SharedBelt), 0);
    if (shm_id == -1) {
        perror("Worker: shmget");
        return 1;
    }   

    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) {
        perror("Worker: shmat");
        return 1;
    }

    // semafory
    int sem_id = semget(get_sem_key(), NUM_SEMS, 0);
    if (sem_id == -1) {
        perror("Worker: semget");
        shmdt(belt);  // clean up
        return 1;
    }

    // kolejka komunikatow
    int msg_id = msgget(get_msg_key(), 0);
    if (msg_id == -1) {
        perror("Worker: msgget");
        // kontynuuj bez kolejki
    }

    // petla pracy
    while (!belt->shutdown && !should_exit) {
        // genorowanie paczki
        Package pkg;
        pkg.type = type;
        pkg.worker_id = getpid();

        // mnieszja paczka = mniejsza waga
        if (type == 'A') {
            pkg.volume = (64 * 38 * 8) / 1000000.0;
            pkg.weight = 0.1 + (rand() % 79) / 10.0;
        }
        else if (type == 'B'){
            pkg.volume = (64 * 38 * 19) / 1000000.0;
            pkg.weight = 8.1 + (rand() % 79) / 10.0;
        }
        else {
            pkg.volume = (64 * 38 * 41) / 1000000.0;
            pkg.weight = 16.1 + (rand() % 89) / 10.0;
        }

        printf("[P%c] Mam paczke %.1f kg, %.6f m3. Czekam na tasme...\n", type, pkg.weight, pkg.volume);

        

        // czekaj na wolne miejsce(tasma pelna = stop)
        if (sem_wait_wrapper(sem_id, SEM_EMPTY) == -1) {
            if (should_exit || belt->shutdown) break;
            continue;
        }

        //sprawdzenie flagi
        if (belt->shutdown) {
            sem_signal(sem_id, SEM_EMPTY);
            break;
        }

        //czekaj na dostep do pamieci(tylko jeden proces na raz moze pisac)
        if (sem_wait_wrapper(sem_id, SEM_MUTEX) == -1) {
            sem_signal(sem_id, SEM_EMPTY);
            if (should_exit || belt->shutdown) break;
            continue;
        }
    
        // sprawdzenie udzwigu tasmy (limit M)
        if (belt->current_weight + pkg.weight > MAX_WEIGHT_BELT) {
            printf("[P%c] Tasma przeciazona! Czekam...\n", type);          
            
            // oddanie semaforow, zeby ciezarowka mogla cos zdjac
            sem_signal(sem_id, SEM_MUTEX);
            sem_signal(sem_id, SEM_EMPTY); 
            
            sleep(2); // odczekaj chwile i sprobuj ponownie
            continue; 
        }  
        
        // kladziemy paczke
        belt->buffer[belt->tail] = pkg;

        // przesuwamy koniec tasmy
        belt->tail = (belt->tail + 1) % MAX_BUFFER_SIZE;

        // update licznikow
        belt->current_count++;
        belt->current_weight += pkg.weight;

        printf("[P%c] Polozono paczke (Waga: %.1f). Stan tasmy: %d/%d szt, %.1f kg\n",
            type, pkg.weight, belt->current_count, MAX_BUFFER_SIZE, belt->current_weight);

        // logowanie do kolejki
        if (msg_id != -1) {
            char log_msg[MSG_MAX_TEXT];
            snprintf(log_msg, MSG_MAX_TEXT, "Worker %c polozyl paczke %.1f kg", type, pkg.weight);
            send_log_message(msg_id, sem_id, log_msg, getpid());
        }

       sem_signal(sem_id, SEM_MUTEX); // oddaj klucz do pamieci
       sem_signal(sem_id, SEM_FULL);  // powiadom ciezarowke, ze jest nowa paczka (FULL + 1)
        
       sleep(rand() % 3 + 1); // symulacja pracy (1-3 sekundy przerwy)
    }
    
    printf("[P%c] Koniec pracy. \n", type);
    shmdt(belt);

    return 0;

}