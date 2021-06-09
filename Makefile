EXE = manager
CC = gcc

all: $(EXE)

$(EXE): manager.c
	$(CC) -Wall -O $^ -o $@

clean:
	rm -f $(EXE)
