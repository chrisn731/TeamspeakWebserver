EXE = manager
CC = gcc

all: $(EXE)

$(EXE): manager.c
	$(CC) -Wall -O2 $^ -o $@

clean:
	rm -f $(EXE)
