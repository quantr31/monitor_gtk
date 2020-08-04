# change application name here (executable output name)
TARGET=template_app

# compiler
CC=gcc
# debug
DEBUG=-g
# optimisation
OPT=-O0
# warnings
WARN=-Wall

PTHREAD=-pthread

CCFLAGS=$(DEBUG) $(OPT) $(WARN) $(PTHREAD) -pipe
CCFLAGS+=`pkg-config --cflags libmodbus`

GTKLIB=`pkg-config --cflags --libs gtk+-3.0`

# linker
LD=gcc
LDFLAGS=$(PTHREAD) $(GTKLIB) -export-dynamic
LDFLAGS+=`pkg-config --libs libmodbus`

OBJS=   main.o gz_clk.o

all: $(OBJS)
	$(LD) -o $(TARGET) $(OBJS) -lbcm2835 -lrt $(LDFLAGS)
    
main.o: src/main.c 
	$(CC) -c $(CCFLAGS) src/main.c $(GTKLIB) -o main.o

gz_clk.o: src/gz_clk.c src/gz_clk.h
	gcc -c src/gz_clk.c
	
    
clean:
	rm -f *.o $(TARGET)
