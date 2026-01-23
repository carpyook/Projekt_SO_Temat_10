#define _XOPEN_SOURCE 700 // potrzebne dla funkcji kill
#include "common.h"
#include <signal.h>
#include <sys/wait.h>

#define NUM_WORKERS 3 // 3 pracownikow
#define NUM_TRUCKS 3 // 3 ciezarowki

int main() {
    printf("START SYMULACJI MAGAZYNU \n");
    
    // Sprawdzenie limitu procesow
    int needed_processes = NUM_WORKERS + NUM_TRUCKS + 2;
    if (check_process_limit(needed_processes) == -1) {
        fprintf(stderr, "Problem z limitem procesow - kontynuuje...\n");
    }
    
    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), IPC_CREAT | 0666);
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
belt->shutdown = 0;

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
pid_t p4_pid = fork();
if (p4_pid == -1) {
    perror("fork p4");
    semctl(sem_id, 0, IPC_RMID);
    shmdt(belt);
    shmctl(shm_id, IPC_RMID, NULL);
    exit(EXIT_FAILURE);
}
if (p4_pid == 0) {
    execl("./worker_p4", "worker_p4", NULL);
    perror("blad execl worker_p4");
    _exit(1);
}

// uruchamianie pracownikow
printf("[MAIN] Uruchamiam pracownikow...\n");
pid_t workers[NUM_WORKERS];
char types[] = {'A', 'B', 'C'};
for (int i = 0; i < NUM_WORKERS; i++) {
    workers[i] = fork();
    if (workers[i] == -1) {
        perror("fork worker");
        fprintf(stderr, "[BLAD] Nie mozna utworzyc pracownika %d\n", i + 1);
        continue;
    }
    if (workers[i] == 0) {
        char type_str[2] = {types[i], '\0'};
        execl("./worker", "worker", type_str, NULL);
        perror("execl worker");
        _exit(1);
    }
    printf("[MAIN] Uruchomiono pracownika P%d (PID: %d, typ: %c)\n", i + 1, workers[i], types[i]);
}

// tworzenie floty ciezarowek
printf("[MAIN] Uruchamiam flote %d ciezarowek...\n", NUM_TRUCKS);
pid_t trucks[NUM_TRUCKS];
for (int i = 0; i < NUM_TRUCKS; i++) {
    trucks[i] = fork();
    if (trucks[i] == -1) {
        perror("fork truck");
        fprintf(stderr, "[BLAD] Nie mozna utworzyc ciezarowki %d\n", i + 1);
        continue;
    }
    if (trucks[i] == 0) {
        execl("./truck", "truck", NULL);
        perror("execl truck");
        _exit(1);
    }
    printf("[MAIN] Uruchomiono ciezarowke %d (PID: %d)\n", i + 1, trucks[i]);
}

// dyspozytor
printf("\n=== MENU DYSPOZYTORA ===\n");
printf(" [1] Wyslij ciezarowke (Sygnal 1)\n");
printf(" [2] Zamow ekspres P4 (Sygnal 2)\n");
printf(" [3] Koniec pracy (Sygnal 3)\n");
    
int cmd;
char input_buffer[100];

while(1) {
    printf("\nPodaj komende (1-3): ");
    fflush(stdout);
    
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
        printf("[INFO] Koniec wejscia \n");
        break;
    }
    
    char *endptr;
    long val = strtol(input_buffer, &endptr, 10);
    
    if (endptr == input_buffer) {
        printf("[BLAD] Nieprawidlowe dane! Podaj liczbe 1, 2 lub 3.\n");
        continue;
    }
    
    if (val < 1 || val > 3) {
        printf("[BLAD] Komenda musi byc z zakresu 1-3!\n");
        continue;
    }
    
    cmd = (int)val;
    
    if (cmd == 1) {
        printf("[MAIN] Wysylam nakaz odjazdu (SIGUSR1) do floty!\n");
        for(int i = 0; i < NUM_TRUCKS; i++) {
            if (trucks[i] > 0) kill(trucks[i], SIGUSR1);
        }
    }
    else if (cmd == 2) {
        printf("[MAIN] Zamawiam ekspres (SIGUSR2)!\n");
        if (p4_pid > 0) kill(p4_pid, SIGUSR2);
    }
    else if (cmd == 3) {
        printf("[MAIN] Koniec pracy!\n");
        break;
    }
}
    
// graceful shutdown
printf("[MAIN] Ustawiam flage zakonczenia pracy...\n");
belt->shutdown = 1;

sleep(1);

// odblokuj procesy czekające na semaforach
for (int i = 0; i < NUM_TRUCKS; i++) {
    sem_signal(sem_id, SEM_FULL);
}
for (int i = 0; i < MAX_BUFFER_SIZE; i++) {
    sem_signal(sem_id, SEM_EMPTY);
}

sleep(1);

// zabijanie procesow
printf("[MAIN] Wysylam SIGTERM do procesow...\n");
for (int i = 0; i < NUM_WORKERS; i++) {
    if (workers[i] > 0) kill(workers[i], SIGTERM);
}
for (int i = 0; i < NUM_TRUCKS; i++) {
    if (trucks[i] > 0) kill(trucks[i], SIGTERM);
}
if (p4_pid > 0) kill(p4_pid, SIGTERM);

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