CC=gcc
CFLAGS = -Wall -std=gnu17

all: scream-jack scream-jack-debug

scream-jack: scream-ivshmem-jack.c
	$(CC) $(CFLAGS) -o scream-ivshmem-jack scream-ivshmem-jack.c -ljack -lpthread -lsamplerate -O2

scream-jack-debug: scream-ivshmem-jack.c
	$(CC) $(CFLAGS) -o scream-ivshmem-jack-debug scream-ivshmem-jack.c -ljack -lpthread -lsamplerate -g3
