CC=gcc
CFLAGS=-c -Wall -pedantic -g
LDFLAGS=
SOURCES=urlyconnect.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=urlyconnect

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)
