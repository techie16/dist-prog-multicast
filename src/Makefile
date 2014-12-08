CC=gcc
IDIR =../include
CFLAGS=-I $(IDIR) 
CFLAGS += -std=c99 -Wall -Wextra -Werror \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wshadow -Wfloat-equal -lpthread -lncurses

ODIR =../obj
LDIR =../lib

_DEPS = client_server.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_CLI_OBJ = client.o client_server.o 
CLI_OBJ = $(patsubst %,$(ODIR)/%,$(_CLI_OBJ))

_SER_OBJ = server.o client_server.o
SER_OBJ = $(patsubst %,$(ODIR)/%,$(_SER_OBJ))

$(ODIR)/%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

client: $(CLI_OBJ)
	$(CC) -g -o $@ $^ $(CFLAGS)

server: $(SER_OBJ)
	$(CC) -g -o $@ $^ $(CFLAGS)

all: client server

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(INCDIR)/*~

rebuild: clean all
