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
belt->current_weight = 0;

printf("[info] tasma jest pusta\n");
sleep (5);

check_error(shmdt(belt), "blad shmdt"); // odlaczenie wskaznika

check_error(shmctl(shm_id, IPC_RMID, NULL), "blad shmctl"); // usuwanie segmentu

printf("KONIEC SYMULACJI \n");
return 0;
}