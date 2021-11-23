CC = gcc
CFLAGS = -O2

all: ltc.c list_sorter.c
	$(CC) $(CFLAGS) $^ -o ltc

debug: ltc.c list_sorter.c
	$(CC) -Wall $^ -o ltc_debug

clean:
	rm -f ltc ltc.o ltc_debug
