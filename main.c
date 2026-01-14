#include "common.h"

int main() {
    printf("START \n");

    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), IPC_CREAT | 0666); // 0666 prawa do odczytu i zapisu dla wszystkich
    check_error(shm_id, "error shmget"); // weryfikacja alokacji

SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
if (belt == (void *)-1) {
    check_error(-1, "blad shmat");
}

printf("[info] Magazyn utworzony ID pamieci: %d\n", shm_id);


belt->head = 0;
belt->tail = 0;
belt->current_count = 0;
belt->current_weight = 0.0;

//zestaw 3 semaforow
int sem_id = semget(SEM_KEY, 3, IPC_CREAT | 0666);
check_error(sem_id, "error semget");
printf("[info] Semafory utworzone ID: %d\n", sem_id);

union semun arg;

// 1. MUTEX = 1 (Dostep do tasmy)
arg.val = 1;
check_error(semctl(sem_id, SEM_MUTEX, SETVAL, arg), "blad init MUTEX");

// 2. EMPTY = MAX_BUFFER_SIZE (Na starcie cala tasma pusta)
arg.val = MAX_BUFFER_SIZE;
check_error(semctl(sem_id, SEM_EMPTY, SETVAL, arg), "blad init EMPTY");

// 3. FULL = 0 (Na starcie zero paczek)
arg.val = 0;
check_error(semctl(sem_id, SEM_FULL, SETVAL, arg), "blad init FULL");

printf("[info] Semafory ustawione: MUTEX=1, EMPTY=%d, FULL=0\n", MAX_BUFFER_SIZE);

//SYMULACJA
printf("[info] tasma jest pusta\n");
sleep (40);

//usuwamy semafory
check_error(semctl(sem_id, 0, IPC_RMID), "blad usuwania semaforow");
printf("[info] Semafory usuniete\n");

check_error(shmdt(belt), "blad shmdt"); // odlaczenie wskaznika

check_error(shmctl(shm_id, IPC_RMID, NULL), "blad shmctl"); // usuwanie segmentu

printf("KONIEC SYMULACJI \n");
return 0;
}