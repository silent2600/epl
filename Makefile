
CC = gcc
LD = gcc
CFLAGS = -std=gnu99 -O2 -Wall -I.
#CFLAGS = -std=gnu99 -ggdb3 -Wall -I.
PERLFLAGS = $(shell perl -MExtUtils::Embed -e ccopts -e perl_inc)
PERLLD = $(shell perl -MExtUtils::Embed -e ldopts)
TARGET = epl.so
RM = rm -fv
ifeq ($(OS),Windows_NT)
	TARGET = epl.dll
	RM = del
endif

all: $(TARGET)

code.h: code.pl
	perl convert_code.pl
epl.o: epl.c code.h
	$(CC) -fPIC -c -o $@ $< $(PERLFLAGS) $(CFLAGS)
$(TARGET): epl.o
	$(LD) -shared -o $@ $^ $(PERLLD)  $(CFLAGS)


clean:
	$(RM) *.o $(TARGET) code.h epl.log


