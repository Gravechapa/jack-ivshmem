CC=gcc
CFLAGS = -Wall -std=gnu17
LDFLAGS= -ljack -lpthread -lm -lsamplerate

all: scream-jack scream-jack-debug

scream-jack: scream-ivshmem-jack.c
	$(CC) $(CFLAGS) -o scream-ivshmem-jack scream-ivshmem-jack.c $(LDFLAGS) -O2

scream-jack-debug: scream-ivshmem-jack.c
	$(CC) $(CFLAGS) -o scream-ivshmem-jack-debug scream-ivshmem-jack.c $(LDFLAGS) -g3
