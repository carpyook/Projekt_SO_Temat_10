#define _XOPEN_SOURCE 700 // potrzebne dla funkcji kill
#include "common.h"
#include <signal.h>
#include <sys/wait.h>

#define NUM_WORKERS 3 // 3 pracownikow
#define NUM_TRUCKS 3 // 3 ciezarowki

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
belt->express_ready = 0;

//zestaw 4 semaforow
int sem_id = semget(SEM_KEY, 4, IPC_CREAT | 0666);
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

// 4. RAMP = 1 (1 = wolna, 0 = zajeta)
arg.val = 1;
check_error(semctl(sem_id, SEM_RAMP, SETVAL, arg), "blad init RAMP");
 
// pracownik ekspres przed innymi
printf("[MAIN] Uruchamiam pracownika P4 (Ekspres)...\n");
    pid_t p4_pid;
    if ((p4_pid = fork()) == 0) {
        execl("./worker_p4", "worker_p4", NULL);
        perror("blad execl worker_p4");
        exit(1);
    }
//uruchamianie pracownikow
printf("[MAIN] Uruchamiam pracownikow...\n");
pid_t workers[NUM_WORKERS];
char types[] = {'A', 'B', 'C'};

for (int i = 0; i < NUM_WORKERS; i++) {
    if ((workers[i] = fork()) == 0) {
        char type_str[2] = {types[i], '\0'};
        execl("./worker", "worker", type_str, NULL);
        exit(1);
    }
}

//tworzenie floty ciezarowek
printf("[MAIN] Uruchamiam flote %d ciezarowek...\n", NUM_TRUCKS);
pid_t trucks[NUM_TRUCKS];
for (int i = 0; i < NUM_TRUCKS; i++) {
    if ((trucks[i] = fork()) == 0) {
        execl("./truck", "truck", NULL);
        exit(1);
    }
}

// dyspozytor
printf("\n=== MENU DYSPOZYTORA ===\n");
printf(" [1] Wyslij ciezarowke (Sygnal 1)\n");
printf(" [2] Zamow ekspres P4 (Sygnal 2)\n");
printf(" [3] Koniec pracy (Sygnal 3)\n");
    
int cmd;
while(1) {
    printf("Podaj komende: ");
    if (scanf("%d", &cmd) != 1) break; 

    if (cmd == 1) {
        printf("[MAIN] Wysylam nakaz odjazdu (SIGUSR1) do floty!\n");
        for(int i=0; i<NUM_TRUCKS; i++) kill(trucks[i], SIGUSR1);
    }
    else if (cmd == 3) {
        printf("[MAIN] Koniec pracy!\n");
        break; 
    }
    else if (cmd == 2) {
        printf("[MAIN] Zamawiam ekspres (SIGUSR2)!\n");
        kill(p4_pid, SIGUSR2);
    }
}
    
// zabijanie procesow
printf("[MAIN] Koniec! Zabijam procesy\n");
for (int i = 0; i < NUM_WORKERS; i++) kill(workers[i], SIGTERM);
for (int i = 0; i < NUM_TRUCKS; i++) kill(trucks[i], SIGTERM);
kill(p4_pid, SIGTERM);

// czekanie na posprzatanie
while(wait(NULL) > 0);

//usuwamy semafory
check_error(semctl(sem_id, 0, IPC_RMID), "blad usuwania semaforow");
printf("[info] Semafory usuniete\n");

check_error(shmdt(belt), "blad shmdt"); // odlaczenie wskaznika

check_error(shmctl(shm_id, IPC_RMID, NULL), "blad shmctl"); // usuwanie segmentu

printf("KONIEC SYMULACJI \n");
return 0;
}