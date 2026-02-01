#define _XOPEN_SOURCE 700 // potrzebne dla funkcji kill
#include "common.h"
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdarg.h>

// konfiguracja symulacji
static const char WORKER_TYPES[] = {'A', 'B', 'C'};
#define NUM_WORKERS ((int)(sizeof(WORKER_TYPES) / sizeof(WORKER_TYPES[0])))
#define NUM_TRUCKS 3 // liczba ciezarowek

// zmienne globalne potrzebne do sprzatania w atexit/signal handlerach
static int g_shm_id = -1;
static int g_sem_id = -1;
static int g_msg_id = -1;
static SharedBelt *g_belt = NULL;

void cleanup_ipc(void) { // zasoby ipc zostana usuniete nawet przy bledzie
    if (g_msg_id != -1) {
        msgctl(g_msg_id, IPC_RMID, NULL);
    }
    if (g_sem_id != -1) {
        semctl(g_sem_id, 0, IPC_RMID);
    }
    if (g_belt != NULL && g_belt != (void*)-1) {
        shmdt(g_belt);
    }
    if (g_shm_id != -1) {
        shmctl(g_shm_id, IPC_RMID, NULL);
    }
}

// handler sigint (ctrl +C)
void handle_sigint(int sig) {
    (void)sig;
    printf(CYAN "\n[MAIN] Otrzymano SIGINT - konczenie pracy...\n" RESET);
    if (g_belt != NULL) {
        g_belt->shutdown = 1; // powiadomienie innych procesow przez pamiec wspoldzielona
    }
    exit(0); // wywolanie atexit(cleanup_ipc)
}
// sprzatanie procesow zombie
void handle_sigchld(int sig) {
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0); // whohang sprawia ze waitpid nie blokuje, jesli nie ma zombie
}
// funkja pomocnicza do zapisu pliku raportu
void write_report(const char *format, ...) {
    int fd = open(REPORT_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("open raport");
        return;
    }

    char buffer[512];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
// formatowanie czasu
    int len = strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", tm_info);

    va_list args;
    va_start(args, format);
    len += vsnprintf(buffer + len, sizeof(buffer) - len, format, args);
    va_end(args);

    if (len < (int)sizeof(buffer) - 1) {
        buffer[len++] = '\n';
    }

    if (write(fd, buffer, len) == -1) {
        perror("write raport");
    }

    close(fd);
}

int main() {
    // funkcja sprzatajaca
    atexit(cleanup_ipc);
    // rejestracja sygnalu
    struct sigaction sa_int;
    sa_int.sa_handler = handle_sigint;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = 0;
    sigaction(SIGINT, &sa_int, NULL);

    struct sigaction sa_chld;
    sa_chld.sa_handler = handle_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    printf(CYAN "START SYMULACJI MAGAZYNU \n" RESET);
    
    // sprawdzenie limitu procesow
    int needed_processes = NUM_WORKERS + NUM_TRUCKS + 2; // +2 to P4 i Logger
    if (check_process_limit(needed_processes) == -1) {
        fprintf(stderr, "Problem z limitem procesow  \n"); // ostrzezenie
    }

    // pamiec wspoldzielona
    int shm_id = shmget(get_shm_key(), sizeof(SharedBelt), IPC_CREAT | 0600);
    g_shm_id = shm_id;
    check_error(shm_id, "error shmget"); // weryfikacja alokacji

SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
g_belt = belt; // globalny wskaznik do sprzatania
if (belt == (void *)-1) {
    check_error(-1, "blad shmat");
}

printf("[info] Magazyn utworzony ID pamieci: %d\n", shm_id);

// inicjalizacja struktury tasmy
belt->head = 0;
belt->tail = 0;
belt->current_count = 0;
belt->current_weight = 0.0;
belt->express_ready = 0;
belt->shutdown = 0;
belt->total_packages = 0;
belt->total_trucks_sent = 0;

// zestaw 6 semaforow
int sem_id = semget(get_sem_key(), NUM_SEMS, IPC_CREAT | 0600);
g_sem_id = sem_id;
check_error(sem_id, "error semget");
printf("[info] Semafory utworzone ID: %d\n", sem_id);

// tworzenie kolejki komunikatow
int msg_id = msgget(get_msg_key(), IPC_CREAT | 0600);
g_msg_id = msg_id;
if (msg_id == -1) {
    perror("msgget");
    semctl(sem_id, 0, IPC_RMID);
    shmdt(belt);
    shmctl(shm_id, IPC_RMID, NULL);
    exit(EXIT_FAILURE);
}

printf("[info] Kolejka komunikatow utworzona ID: %d\n", msg_id);

// tworzenie raportu
int report_fd = open(REPORT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
if (report_fd == -1) {
    perror("open raport");
} else {
    const char *header = "=== RAPORT SYMULACJI MAGAZYNU ===\n\n";
    if (write(report_fd, header, strlen(header)) == -1) {
        perror("write header");
    }
    close(report_fd);
    printf("[info] Plik raportu utworzony: %s\n", REPORT_FILE);
}

write_report("Symulacja rozpoczeta");

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

// 5. REPORT = 1 (Dostep do pliku raportu)
arg.val = 1;
check_error(semctl(sem_id, SEM_REPORT, SETVAL, arg), "blad init REPORT");

// 6. MSG_GUARD = MAX_MSG_QUEUE (Straznik kolejki komunikatow)
arg.val = MAX_MSG_QUEUE;
check_error(semctl(sem_id, SEM_MSG_GUARD, SETVAL, arg), "blad init MSG_GUARD");

//uruchomienie procesow

// uruchomienie loggera jako pierwszy zeby lapal logi
printf(CYAN "[MAIN] Uruchamiam proces logowania...\n" RESET);
pid_t logger_pid = fork();
if (logger_pid == -1) {
    perror("fork logger");
    semctl(sem_id, 0, IPC_RMID);
    msgctl(msg_id, IPC_RMID, NULL);
    shmdt(belt);
    shmctl(shm_id, IPC_RMID, NULL);
    exit(EXIT_FAILURE);
}
if (logger_pid == 0) {
    execl("./logger", "logger", NULL);
    perror("blad execl logger");
    _exit(1);
}
printf(CYAN "[MAIN] Uruchomiono logger (PID: %d)\n" RESET, logger_pid);
write_report("Uruchomiono logger (PID: %d)", logger_pid);

// czas na inicjalizacje loggera (otwarcie pliku i kolejki)
sleep(1);

// pracownik ekspres przed innymi
printf(CYAN "[MAIN] Uruchamiam pracownika P4 (Ekspres)...\n" RESET);
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
printf(CYAN "[MAIN] Uruchomiono pracownika P4 (PID: %d)\n" RESET, p4_pid);
write_report("Uruchomiono pracownika P4 (Ekspres, PID: %d)", p4_pid);

// uruchamianie pracownikow
printf(CYAN "[MAIN] Uruchamiam pracownikow...\n" RESET);
pid_t workers[NUM_WORKERS];
for (int i = 0; i < NUM_WORKERS; i++) {
    workers[i] = fork();
    if (workers[i] == -1) {
        perror("fork worker");
        fprintf(stderr, "[BLAD] Nie mozna utworzyc pracownika %d\n", i + 1);
        continue;
    }
    if (workers[i] == 0) {
        char type_str[2] = {WORKER_TYPES[i], '\0'};
        execl("./worker", "worker", type_str, NULL);
        perror("execl worker");
        _exit(1);
    }
    printf(CYAN "[MAIN] Uruchomiono pracownika P%d (PID: %d, typ: %c)\n" RESET, i + 1, workers[i], WORKER_TYPES[i]);
    write_report("Uruchomiono pracownika P%d (typ %c, PID: %d)", i + 1, WORKER_TYPES[i], workers[i]);
}

// tworzenie floty ciezarowek
printf(CYAN "[MAIN] Uruchamiam flote %d ciezarowek...\n" RESET, NUM_TRUCKS);
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
    printf(CYAN "[MAIN] Uruchomiono ciezarowke %d (PID: %d)\n" RESET, i + 1, trucks[i]);
    write_report("Uruchomiono ciezarowke %d (PID: %d)", i + 1, trucks[i]);
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
        printf(CYAN "[MAIN] Wysylam nakaz odjazdu (SIGUSR1) do floty!\n" RESET);
        // wysylamy do wszystkich, te w kolejce zignoruja, ta na rampie odjezdza
        for(int i = 0; i < NUM_TRUCKS; i++) {
            if (trucks[i] > 0) {
                if (kill(trucks[i], SIGUSR1) == -1) {
                    perror("kill SIGUSR1");
                }
            }
        }
        write_report("DYSPOZYTOR: Sygnal 1 - wymuszony odjazd");
    }
    else if (cmd == 2) {
        printf(CYAN "[MAIN] Zamawiam ekspres (SIGUSR2)!\n" RESET);
        if (p4_pid > 0) {
            if (kill(p4_pid, SIGUSR2) == -1) {
                perror("kill SIGUSR2");
            }
        }
        write_report("DYSPOZYTOR: Sygnal 2 - zamowienie ekspresu");
    }
    else if (cmd == 3) {
        printf(CYAN "[MAIN] Koniec pracy!\n" RESET);
        write_report("DYSPOZYTOR: Sygnal 3 - koniec pracy");
        break;
    }
}
    
// graceful shutdown
printf(CYAN "[MAIN] Ustawiam zakonczenie pracy...\n" RESET);
belt->shutdown = 1;

// czas na wyslanie shutdown do procesow
sleep(1);

// odblokuj procesy czekające na semaforach
for (int i = 0; i < NUM_TRUCKS; i++) {
    sem_signal(sem_id, SEM_FULL);
}
for (int i = 0; i < MAX_BUFFER_SIZE; i++) {
    sem_signal(sem_id, SEM_EMPTY);
}

// czas na wybudzenie procesow czekajacych na semaforach
sleep(1);

// czekaj az trucks oproznia tasme
printf(CYAN "[MAIN] Czekam na oprozninie tasmy (pozostalo: %d paczek)...\n" RESET,
       belt->current_count);

long iterations = 0;
long max_iterations = 100000000L; // zabezpieczenie przed nieskonczona petla

while ((belt->current_count > 0 || belt->express_ready) && iterations < max_iterations) {
    iterations++;
}

if (belt->current_count == 0 && !belt->express_ready) {
    printf(CYAN "[MAIN] Tasma oprozniona - wszystkie paczki rozwiezione!\n" RESET);
} else {
    printf(CYAN "[MAIN] TIMEOUT: %d paczek zostalo na tasmie.\n" RESET,
           belt->current_count);
}

// zabijanie procesow
printf(CYAN "[MAIN] Wysylam SIGTERM do procesow...\n" RESET);
for (int i = 0; i < NUM_WORKERS; i++) {
    if (workers[i] > 0) {
        if (kill(workers[i], SIGTERM) == -1 && errno != ESRCH) {
            perror("kill SIGTERM worker");
        }
    }
}
for (int i = 0; i < NUM_TRUCKS; i++) {
    if (trucks[i] > 0) {
        if (kill(trucks[i], SIGTERM) == -1 && errno != ESRCH) {
            perror("kill SIGTERM truck");
        }
    }
}
if (p4_pid > 0) {
    if (kill(p4_pid, SIGTERM) == -1 && errno != ESRCH) {
        perror("kill SIGTERM p4");
    }
}

// czekanie na zakonczenie wszystkich procesow (poza loggerem)
printf(CYAN "[MAIN] Czekam na zakonczenie procesow...\n" RESET);
long wait_iterations = 0;
long max_wait_iterations = 50000000L; // zabezpieczenie

while (wait_iterations < max_wait_iterations) {
    pid_t result = waitpid(-1, NULL, WNOHANG);
    if (result > 0) {
        // zebrano proces
        printf(CYAN "[MAIN] Proces %d zakonczony\n" RESET, result);
    } else if (result == -1 && errno == ECHILD) {
        // brak procesow potomnych
        printf(CYAN "[MAIN] Wszystkie procesy potomne zakonczone\n" RESET);
        break;
    }
    wait_iterations++;
}

// czas na odebranie przez logger pozostalych logow z kolejki
sleep(2);

// zabijanie loggera
printf(CYAN "[MAIN] Wysylam SIGTERM do loggera...\n" RESET);
if (logger_pid > 0) {
    if (kill(logger_pid, SIGTERM) == -1 && errno != ESRCH) {
        perror("kill SIGTERM logger");
    }
}

wait(NULL);

write_report("Symulacja zakonczona");
// statystyki
printf(GREEN "\n=== STATYSTYKI ===\n");
printf("Zaladowano paczek: %d\n", belt->total_packages);
printf("Wyslano ciezarowek: %d\n", belt->total_trucks_sent);
printf("==================\n\n" RESET);

write_report("Zaladowano paczek: %d", belt->total_packages);
write_report("Wyslano ciezarowek: %d", belt->total_trucks_sent);

// usuwanie kolejki komunikatow
if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
    perror("msgctl IPC_RMID");
} else {
    printf("[info] Kolejka komunikatow usunieta\n");
}

//usuwamy semafory
check_error(semctl(sem_id, 0, IPC_RMID), "blad usuwania semaforow");
printf("[info] Semafory usuniete\n");

check_error(shmdt(belt), "blad shmdt"); // odlaczenie wskaznika

check_error(shmctl(shm_id, IPC_RMID, NULL), "blad shmctl"); // usuwanie segmentu

printf(GREEN "KONIEC SYMULACJI \n" RESET);
return 0;
}