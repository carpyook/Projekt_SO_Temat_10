#define _POSIX_C_SOURCE 200809L // usleep
#define _XOPEN_SOURCE 700
#include "common.h"
#include <time.h>
#include <signal.h>

// limity ciezarowki
#define TRUCK_CAPACITY_KG 500.0      // 500 kg
#define TRUCK_CAPACITY_M3 30        // 30m^3
#define RETURN_TIME 10               // czas dostawy s

volatile sig_atomic_t force_departure = 0; 
volatile sig_atomic_t should_exit = 0;

void handle_sigusr1(int sig) {
    (void)sig;
    force_departure = 1; // wymuszony odjazd
}

void handle_sigterm(int sig) {
    (void)sig;
    should_exit = 1;
}

int main() {
    srand(time(NULL) ^ getpid());
    printf("[TRUCK %d] Ciezarowka podjezdza. \n", getpid());

    //rejestracja sygnalu (gdy przyjdzie SIGUSR1, uruchom hanlde_sigusr1)
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Bez flagi SA_RESTART, zeby przerwac sem_wait
    sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);

    // pobranie zasobow
    int shm_id = shmget(get_shm_key(), sizeof(SharedBelt), 0);
    if (shm_id == -1) {
        perror("Truck: shmget");
        return 1;
    }
    
   SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) {
        perror("Truck: shmat");
        return 1;
    }

    int sem_id = semget(get_sem_key(), NUM_SEMS, 0);
    if (sem_id == -1) {
        perror("Truck: semget");
        shmdt(belt); // clean up
    return 1;
    }

    // kolejka komunikatow
    int msg_id = msgget(get_msg_key(), 0);
    if (msg_id == -1) {
        perror("Truck: msgget");
        // kontynuuj bez kolejki
    }

    // petla pracy ciezarowki
    // kontynuuj prace dopoki sa paczki na tasmie, nawet po shutdown
    while ((!belt->shutdown || belt->current_count > 0 || belt->express_ready) && !should_exit) {

        printf("[TRUCK %d] Czekam w kolejce do rampy...\n", getpid());

        if (sem_wait_wrapper(sem_id, SEM_RAMP) == -1) {
            if (should_exit) break;
            continue;
        }

        if (should_exit) {
            sem_signal(sem_id, SEM_RAMP);
            break;
        }

        // Sprawdz czy sa jeszcze paczki do zabrania
        if (belt->shutdown && belt->current_count == 0 && !belt->express_ready) {
            sem_signal(sem_id, SEM_RAMP);
            break;
        }

        printf("[TRUCK %d] Wjechalem na rampe! Zaczynam zaladunek.\n", getpid());
        force_departure = 0;

        float current_weight = 0.0;   // kg
        float current_volume = 0.0;   // m^3
        int package_count = 0;

        // petla zaladunku
        // kontynuuj ladowanie nawet po shutdown, dopoki sa paczki
        while ((!belt->shutdown || belt->current_count > 0 || belt->express_ready) && !should_exit && !force_departure) {

            if (current_weight >= TRUCK_CAPACITY_KG * 0.95 ||
                current_volume >= TRUCK_CAPACITY_M3 * 0.95) {
                printf("[TRUCK %d] Pelna!\n", getpid());
                break;
            }

            // czekaj na paczke
            printf("[TRUCK %d] Czekam na paczki...\n", getpid());

            if (sem_wait_wrapper(sem_id, SEM_FULL) == -1) {
                if (force_departure || should_exit) break;
                if (belt->shutdown && belt->current_count == 0 && !belt->express_ready) break;
                continue;
            }

            if (should_exit) {
                sem_signal(sem_id, SEM_FULL);
                break;
            }

            // sprawdz czy sa jeszcze paczki po shutdown
            if (belt->shutdown && belt->current_count == 0 && !belt->express_ready) {
                sem_signal(sem_id, SEM_FULL);
                break;
            }

            if (sem_wait_wrapper(sem_id, SEM_MUTEX) == -1) {
                sem_signal(sem_id, SEM_FULL); // oddajemy paczke bo jej nie wzielismy
                if (force_departure || should_exit) break;
                if (belt->shutdown && belt->current_count == 0 && !belt->express_ready) break;
                continue;
            }
            
            //sprawdzenie czy jest ekspres
            if (belt->express_ready == 1) {
                Package exp = belt->express_pkg;

                //sprawdzenie czy sie zmiesci
                if (current_weight + exp.weight <= TRUCK_CAPACITY_KG && current_volume + exp.volume <= TRUCK_CAPACITY_M3) {

                    //ladowanie ekspresu
                    current_weight += exp.weight;
                    current_volume += exp.volume;
                    package_count++;

                    belt->express_ready = 0; // oznaczamy jako odebrana
                    printf("[TRUCK %d] !!! EKSPRES !!! Zaladowano (%.1f kg). Stan: %d\n", getpid(), exp.weight, package_count);

                    // logowanie do kolejki
                    if (msg_id != -1) {
                        char log_msg[MSG_MAX_TEXT];
                        snprintf(log_msg, MSG_MAX_TEXT, "Truck %d zaladowal EKSPRES %.1f kg", getpid(), exp.weight);
                        send_log_message(msg_id, sem_id, log_msg, getpid());
                    }

                    sem_signal(sem_id, SEM_MUTEX); // oddanie mutexu
                    continue;
                } 
            }

            // pobierz paczke z head
            if (belt->current_count == 0) {
                sem_signal(sem_id, SEM_MUTEX);
                sem_signal(sem_id, SEM_FULL);
                usleep(100000);
                continue;
            }
            Package pkg = belt->buffer[belt->head];
            
            // sprawdzenie czy sie zmiesci
            if (current_weight + pkg.weight > TRUCK_CAPACITY_KG ||
                current_volume + pkg.volume > TRUCK_CAPACITY_M3) {
                
                printf("[TRUCK] Paczka %c (%.1f kg, %.6f m3) sie NIE zmiesci. "
                       "Koncze zaladunek.\n", 
                       pkg.type, pkg.weight, pkg.volume);
                
                sem_signal(sem_id, SEM_MUTEX);
                sem_signal(sem_id, SEM_FULL);
                break; // koniec zaladunku
            }
            
            // paczka sie zmiesci, zabierz z tasmy
            belt->head = (belt->head + 1) % MAX_BUFFER_SIZE;
            belt->current_count--;
            belt->current_weight -= pkg.weight;
            
            // zaladuj na ciezarowke
            current_weight += pkg.weight;
            current_volume += pkg.volume;
            package_count++;
            belt->total_packages++;
            
            printf("[TRUCK] Zaladowano paczke %c (%.1f kg, %.6f m3). "
                   "Stan ciezarowki: %d paczek, %.1f kg, %.6f m3\n",
                   pkg.type, pkg.weight, pkg.volume,
                   package_count, current_weight, current_volume);

            // logowanie do kolejki
            if (msg_id != -1) {
                char log_msg[MSG_MAX_TEXT];
                snprintf(log_msg, MSG_MAX_TEXT, "Truck %d zaladowal paczke %c %.1f kg", getpid(), pkg.type, pkg.weight);
                send_log_message(msg_id, sem_id, log_msg, getpid());
            }

            sem_signal(sem_id, SEM_MUTEX);  // oddaj klucz
            sem_signal(sem_id, SEM_EMPTY);  // zwolnij miejsce
            
            usleep(500000); // symulacja czasu zaladunku jednej paczki
        }
        
        if (package_count > 0) {
            belt->total_trucks_sent++;
            printf("[TRUCK %d] ODJAZD! %d paczek, %.1f kg\n", getpid(), package_count, current_weight);

            // logowanie do kolejki
            if (msg_id != -1) {
                char log_msg[MSG_MAX_TEXT];
                snprintf(log_msg, MSG_MAX_TEXT, "Truck %d odjechal z %d paczkami (%.1f kg)", getpid(), package_count, current_weight);
                send_log_message(msg_id, sem_id, log_msg, getpid());
            }

            sem_signal(sem_id, SEM_RAMP);

            sleep(RETURN_TIME);

            printf("[TRUCK %d] Wrocilem.\n", getpid());

        } else {
            printf("[TRUCK %d] Brak paczek.\n", getpid());
            sem_signal(sem_id, SEM_RAMP);
            sleep(2);
        }       
    }
    
    // nie dojedzie tu ale dla porządku
    printf("[TRUCK %d] Koniec pracy. \n", getpid());
    shmdt(belt);
    return 0;
}