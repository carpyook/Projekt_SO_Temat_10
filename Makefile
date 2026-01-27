CC = gcc
CFLAGS = -Wall -Wextra -g

TARGETS = main worker worker_p4 truck logger

all: $(TARGETS)

# main
main: main.c common.h
	$(CC) $(CFLAGS) main.c -o main

# worker
worker: worker.c common.h
	$(CC) $(CFLAGS) worker.c -o worker

# worker_p4
worker_p4: worker_p4.c common.h
	$(CC) $(CFLAGS) worker_p4.c -o worker_p4

# truck
truck: truck.c common.h
	$(CC) $(CFLAGS) truck.c -o truck

# logger
logger: logger.c common.h
	$(CC) $(CFLAGS) logger.c -o logger


run: all
	./main

clean:
	rm -f $(TARGETS)
	rm -f raport_symulacji.txt

.PHONY: all run clean
