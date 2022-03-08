CC = gcc

manager: manager.c session.c client.c
	$(CC) -O2 -Wall $^ -o $@

debug: manager.c session.c
	$(CC) -Wall -ggdb3 $^ -o $@

clean:
	rm -f debug manager
