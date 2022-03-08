CC = gcc
LTC_DIR = ./ltc
MAN_DIR = ./man
WEBSERVER_DIR = ./webserver

all: manager webserver ltc
	$(info Done.)

manager: manager.c
	$(MAKE) -C $(MAN_DIR)
	mv $(MAN_DIR)/manager .

webserver:
	$(MAKE) -C $(WEBSERVER_DIR)

ltc:
	$(MAKE) -C $(LTC_DIR)

clean:
	rm -f $(EXE)

.PHONY: ltc webserver
