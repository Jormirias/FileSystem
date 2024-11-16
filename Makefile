
OBJ=fso-sh.o fs.o disk.o bitmap.o
CFLAGS=-Wall -g

all: fso-sh


fso-sh: fso-sh.o fs.o disk.o bitmap.o
	cc -g $(OBJ) -o fso-sh

clean:
	rm -f fso-sh $(OBJ) *~
