#Makefile del progetto
VERSION = 1.0

CC       = gcc
OPTIMIZE = -Wall -pedantic
#OPTIMIZE = -O3 -s -fomit-frame-pointer
CFLAGS   = $(OPTIMIZE)
PROGS	 = file1.c fileA.c fileB.c
#PROGS_O  = fileA.o fileB.o

LIBS     = util.h



file1: file1.c
	$(CC) $(CFLAGS) file1.c -o file1
fileA: fileA.c
	$(CC) $(CFLAGS) fileA.c -o fileA
fileB: fileB.c
	$(CC) $(CFLAGS) fileB.c -o fileB
