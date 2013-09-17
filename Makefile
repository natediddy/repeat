CC = gcc
CFLAGS = -Wall -O2 -march=native -mtune=native
TARGET = repeat

prefix = /usr/local

all: $(TARGET)

$(TARGET): repeat.o
	$(CC) $(CFLAGS) repeat.o -o $(TARGET)

repeat.o: repeat.c
	$(CC) $(CFLAGS) -c repeat.c

clean:
	@rm -f repeat.o

clobber:
	@rm -f $(TARGET) repeat.o

install: $(TARGET)
	install -m 755 $(TARGET) $(prefix)/bin/$(TARGET)

uninstall:
	rm -f $(prefix)/bin/$(TARGET)

.PHONY: clean clobber install uninstall
