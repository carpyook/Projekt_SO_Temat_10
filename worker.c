#include "common.h"
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("uzyj ./worker [TYP A/B/C]\n");
        return 1;

    }
    char type = argv[1][0]; // pobranie pierwszej litery
    srand(time(NULL) ^ getpid());

    printf("[P%c] pracownik uruchomiony, PID: %d\n", type, getpid());

    // pobranie zasobow

    // pamiec
    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), 0);
    check_error(shm_id, "Worker: blad shmget");

    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) check_error(-1, "Worker: blad shmat");

    // semafory
    int sem_id = semget(SEM_KEY, 4, 0);
    check_error(sem_id, "Worker: blad semget");

    // petla pracy
    while (1){
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
        sem_wait(sem_id, SEM_EMPTY);

        //czekaj na dostep do pamieci(tylko jeden proces na raz moze pisac)
        sem_wait(sem_id, SEM_MUTEX);
    
        // sprawdzenie udzwigu tasmy (limit M)
        if (belt->current_weight + pkg.weight > MAX_WEIGHT_BELT) {
            printf("[P%c] BLAD: Tasma przeciazona! (%.1ff + %.1f >  %.1f). Czekam...\n", 
                 type, belt->current_weight, pkg.weight, MAX_WEIGHT_BELT);
                
            
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


       sem_signal(sem_id, SEM_MUTEX); // oddaj klucz do pamieci
       sem_signal(sem_id, SEM_FULL);  // powiadom ciezarowke, ze jest nowa paczka (FULL + 1)
        
       sleep(rand() % 3 + 1); // symulacja pracy (1-3 sekundy przerwy)
    }

    return 0;

}