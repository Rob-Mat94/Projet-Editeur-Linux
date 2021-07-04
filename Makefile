EXEC=editor.exe
SOURCES=editor.c
OBJECTS=$(SOURCES:.c=.o)
CC=gcc
CFLAGS=-Wall -Werror -g -std=gnu99
.PHONY: default clean

default: $(EXEC)

editor.o: editor.c 


%.o: %.c
	$(CC) -o $@ -c $< $(CFLAGS)

$(EXEC): $(OBJECTS)
	$(CC) -o $@ $^

clean:
	rm -rf $(EXEC) $(OBJECTS) $(SOURCES:.c=.c~) $(SOURCES:.c=.h~) Makefile~