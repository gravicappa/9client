MKSHELL = rc

name = 9client

CC = gcc
CFLAGS = -O0 -g -Wall
#LDFLAGS =
O = .o

obj = 9pmsg$O seq$O 9pconn$O util$O client$O

$name: $obj
  $CC $CFLAGS $prereq $LDFLAGS -o $target

%$O: %.c
  $CC $CFLAGS -c $stem.c -o $target
