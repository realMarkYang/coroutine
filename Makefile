all : main

main : main.c coroutine.c
	gcc -g -Wall -o $@ $^

test : test.c
	gcc -g -Wall -o $@ $^

clean :
	rm -rf main test

.PHONY : all main clean test 