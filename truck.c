#include "common.h"
#include <time.h>

// limity ciezarowki
#define TRUCK_CAPACITY_KG 500.0      // 500 kg
#define TRUCK_CAPACITY_M3 30        // 30m^3
#define RETURN_TIME 10               // czas dostawy s

int main() {
    printf("[TRUCK] Ciezarowka podjezdza. PID: %d\n", getpid());
    
    // pobranie zasobow
    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), 0);
    check_error(shm_id, "Truck: blad shmget");
    
    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) check_error(-1, "Truck: blad shmat");
    
    int sem_id = semget(SEM_KEY, 3, 0);
    check_error(sem_id, "Truck: blad semget");
    
    // petla pracy ciezarowki
    while (1) {
        float current_weight = 0.0;   // kg
        float current_volume = 0.0;       // m^3
        int package_count = 0;
        
        printf("[TRUCK] Rozpoczynam zaladunek...\n");
        
        // petla zaladunku
        while (1) {
            // czekaj na paczke
            printf("[TRUCK] Czekam na paczki...\n");
            sem_wait(sem_id, SEM_FULL);
            
            // blokada dostepu do pamieci
            sem_wait(sem_id, SEM_MUTEX);
                        
            // pobierz paczke z head
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
            
            printf("[TRUCK] Zaladowano paczke %c (%.1f kg, %.6f m3). "
                   "Stan ciezarowki: %d paczek, %.1f kg, %.6f m3\n",
                   pkg.type, pkg.weight, pkg.volume,
                   package_count, current_weight, current_volume);
            
            
            sem_signal(sem_id, SEM_MUTEX);  // oddaj klucz
            sem_signal(sem_id, SEM_EMPTY);  // zwolnij miejsce
            
            sleep(1); // symulacja czasu zaladunku jednej paczki
        }
        
        // jazda ciezarowki
        printf("[TRUCK] Pelna! Jade dostarczyc %d paczek (%.1f kg, %.6f m3). "
               "Wracam za %d sekund!\n",
               package_count, current_weight, current_volume, RETURN_TIME);
        
        sleep(RETURN_TIME); // symulacja dostawy
        
        printf("[TRUCK] Wrocilem do magazynu! Gotowy do kolejnego zaladunku.\n\n");
        
    }
    
    // nie dojedzie tu ale dla porządku
    shmdt(belt);
    return 0;
}