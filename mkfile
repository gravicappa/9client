MKSHELL = rc

name = client

CC = gcc
CFLAGS = -O0 -g -Wall
#LDFLAGS =
O = .o

obj = 9pmsg$O 9pdbg$O seq$O 9pconn$O util$O client$O

$name: $obj
  $CC $CFLAGS $prereq $LDFLAGS -o $target

%$O: %.c
  $CC $CFLAGS -c $stem.c -o $target
