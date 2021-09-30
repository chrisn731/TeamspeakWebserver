CC = gcc
LTC_DIR = ./ltc
WEBSERVER_DIR = ./webserver

all: manager webserver ltc
	$(info Done.)

manager: manager.c
	$(CC) -Wall -O2 $^ -o $@

webserver:
	$(MAKE) -C $(WEBSERVER_DIR)

ltc:
	$(MAKE) -C $(LTC_DIR)

clean:
	rm -f $(EXE)

.PHONY: ltc webserver
