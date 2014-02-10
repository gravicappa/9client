MKSHELL = rc

name = client
lib = lib9pc.a

CC = gcc
AR = ar
RANLIB = ranlib
CFLAGS = -O0 -g -Wall
#LDFLAGS =
O = .o
<$platform.mk

obj = 9pmsg$O 9pdbg$O seq$O 9pconn$O util$O

all:V: $name 

$name: client$O $lib 
  $CC $CFLAGS $prereq $LDFLAGS -o $target

$lib: $obj
  $AR rcu $target $prereq
  $RANLIB $target

%$O: %.c
  $CC $CFLAGS -c $stem.c -o $target
