UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    NASM_FMT := macho64
else
    NASM_FMT := elf64
endif

all : main

coctx.o : coctx.asm
	nasm -f $(NASM_FMT) -g -o $@ $<

main : main.c coroutine.c coctx.o
	gcc -g -Wall -o $@ $^

test : test.c
	gcc -g -Wall -o $@ $^

clean :
	rm -rf main test *.o

.PHONY : all clean test