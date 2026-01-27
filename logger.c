#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <signal.h>

volatile sig_atomic_t should_exit = 0;

void handle_sigterm(int sig) {
    (void)sig;
    should_exit = 1;
}

int main() {
    printf("[LOGGER] Proces logowania uruchomiony (PID: %d)\n", getpid());

    // obsluga SIGTERM
    struct sigaction sa;
    sa.sa_handler = handle_sigterm;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    // pobranie zasobow
    int shm_id = shmget(get_shm_key(), sizeof(SharedBelt), 0);
    if (shm_id == -1) {
        perror("Logger: shmget");
        return 1;
    }

    SharedBelt *belt = (SharedBelt *)shmat(shm_id, NULL, 0);
    if (belt == (void *)-1) {
        perror("Logger: shmat");
        return 1;
    }

    int sem_id = semget(get_sem_key(), NUM_SEMS, 0);
    if (sem_id == -1) {
        perror("Logger: semget");
        shmdt(belt);
        return 1;
    }

    int msg_id = msgget(get_msg_key(), 0);
    if (msg_id == -1) {
        perror("Logger: msgget");
        shmdt(belt);
        return 1;
    }

    // otwarcie pliku raportu do dopisywania
    int fd = open(REPORT_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("Logger: open raport");
        shmdt(belt);
        return 1;
    }

    printf("[LOGGER] Rozpoczynam odbior logow...\n");

    // petla odbioru logow
    while (!belt->shutdown && !should_exit) {
        LogMessage msg;
        int ret = receive_log_message(msg_id, sem_id, &msg);

        if (ret == 1) {
            // odebrano wiadomosc, zapisz do pliku
            struct tm *tm_info = localtime(&msg.timestamp);
            char buffer[512];

            int len = strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", tm_info);
            len += snprintf(buffer + len, sizeof(buffer) - len, "(PID %d) %s\n",
                          msg.sender_pid, msg.text);

            if (write(fd, buffer, len) == -1) {
                perror("Logger: write");
            }
        } else if (ret == 0) {
            // brak wiadomosci, czekaj chwile
            usleep(100000); // 0.1s
        } else {
            // blad
            break;
        }
    }

    // przed zakonczeniem, odbierz pozostale logi
    printf("[LOGGER] Odbieranie pozostalych logow...\n");
    int remaining = 0;
    while (1) {
        LogMessage msg;
        int ret = receive_log_message(msg_id, sem_id, &msg);

        if (ret != 1) break;

        remaining++;
        struct tm *tm_info = localtime(&msg.timestamp);
        char buffer[512];

        int len = strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", tm_info);
        len += snprintf(buffer + len, sizeof(buffer) - len, "(PID %d) %s\n",
                      msg.sender_pid, msg.text);

        if (write(fd, buffer, len) == -1) {
            perror("Logger: write");
        }
    }

    if (remaining > 0) {
        printf("[LOGGER] Odebrano %d pozostalych logow.\n", remaining);
    }

    close(fd);
    shmdt(belt);

    printf("[LOGGER] Koniec pracy.\n");
    return 0;
}
