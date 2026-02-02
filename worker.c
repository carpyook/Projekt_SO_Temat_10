#define _POSIX_C_SOURCE 200809L // sigaction
#include "common.h"
#include <time.h>
#include <signal.h>

volatile sig_atomic_t should_exit = 0;

void handle_sigterm(int sig) { // prosba o zakonczenie
    (void)sig;
    should_exit = 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { //sprawdzenie czy podano argument startowy
        fprintf(stderr, "Uzycie: %s [TYP A/B/C]\n", argv[0]);
        return 1;

    }
    char type = argv[1][0]; // pobranie pierwszej litery
    if (type != 'A' && type != 'B' && type != 'C') {
        fprintf(stderr, "[BLAD] Nieprawidlowy typ paczki: %c\n", type);
        return 1;
    }

    srand(time(NULL) ^ getpid()); // inicjalizacja generatora libcz losowych

    // wybor koloru dla workera
    const char *color = (type == 'A') ? GREEN : (type == 'B') ? YELLOW : MAGENTA;
    //rejestracja sygnalu
    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);


    printf("%s[P%c] pracownik uruchomiony, PID: %d\n" RESET, color, type, getpid());

    // pobranie zasobow

    // pamiec wspoldzielona
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
    }

    // petla pracy
    while (!belt->shutdown && !should_exit) {
        // genorowanie paczki
        Package pkg;
        pkg.type = type;
        pkg.worker_id = getpid();

        // mnieszja paczka = mniejsza waga, losowanie wagi
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

        printf("%s[P%c] Mam paczke %.1f kg, %.6f m3. Czekam na tasme...\n" RESET, color, type, pkg.weight, pkg.volume);

        

        // czekaj na wolne miejsce(tasma pelna = stop)
        if (sem_wait_wrapper(sem_id, SEM_EMPTY) == -1) {
            if (should_exit || belt->shutdown) break; // przerwane przez zamkniecie systemu
            continue;
        }

        //sprawdzenie czy nie zamknieto magazynu w miedzyczasie
        if (belt->shutdown) {
            sem_signal(sem_id, SEM_EMPTY);
            break;
        }

        //czekaj na dostep do pamieci(tylko jeden proces na raz moze modyfikowac)
        if (sem_wait_wrapper(sem_id, SEM_MUTEX) == -1) {
            sem_signal(sem_id, SEM_EMPTY);
            if (should_exit || belt->shutdown) break;
            continue;
        }
    
        // sprawdzenie udzwigu tasmy (limit M)
        if (belt->current_weight + pkg.weight > MAX_WEIGHT_BELT) {
            printf("%s[P%c] Tasma przeciazona! Czekam...\n" RESET, color, type);          
            
            // oddanie semaforow, zeby ciezarowka mogla cos zdjac
            sem_signal(sem_id, SEM_MUTEX);
            sem_signal(sem_id, SEM_EMPTY); 
            
            sleep(2); // oczekiwanie az ciezarowka zdejmie paczki z tasmy
            continue; 
        }  
        
        // kladziemy paczke
        belt->buffer[belt->tail] = pkg;

        // przesuwamy koniec tasmy
        belt->tail = (belt->tail + 1) % MAX_BUFFER_SIZE;

        // update licznikow
        belt->current_count++;
        belt->current_weight += pkg.weight;

        printf("%s[P%c] Polozono paczke (Waga: %.1f). Stan tasmy: %d/%d szt, %.1f kg\n" RESET,
            color, type, pkg.weight, belt->current_count, MAX_BUFFER_SIZE, belt->current_weight);

        // wyslanie logu
        if (msg_id != -1) {
            char log_msg[MSG_MAX_TEXT];
            snprintf(log_msg, MSG_MAX_TEXT, "Worker %c polozyl paczke %.1f kg", type, pkg.weight);
            if (send_log_message(msg_id, sem_id, log_msg, getpid()) == -1) {
                printf(RED "[P%c] WARN: Kolejka komunikatow pelna - log odrzucony!\n" RESET, type);
            }
        }

       sem_signal(sem_id, SEM_MUTEX); // oddaj klucz do pamieci
       sem_signal(sem_id, SEM_FULL);  // powiadom ciezarowke, ze jest nowa paczka
        
       sleep(rand() % 3 + 1); // symulacja czasu potrzebnego na przygotowanie nastepnej paczki
    }
    
    printf("%s[P%c] Koniec pracy. \n" RESET, color, type);
    shmdt(belt);// odlaczenie pamieci wspoldzielonej

    return 0;

}