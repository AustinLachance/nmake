CC = gcc
CFLAGS = -g3 -std=c99 -pedantic -Wall
HWK    = /c/cs323/Hwk1

nmake: nmake.o
	${CC} ${CFLAGS} ${HWK}/getLine.o -o nmake nmake.o

