#include "common.h"

int main() {
    printf("START \n");

    int shm_id = shmget(SHM_KEY, sizeof(SharedBelt), IPC_CREAT | 0666); // 0666 prawa do odczytu i zapisu dla wszystkich
    check_error(shm_id, "error shmget");

}