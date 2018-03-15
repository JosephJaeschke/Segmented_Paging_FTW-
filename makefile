all:
	gcc -g -o phase2 phase2.c
malloc:
	gcc -Wall -g -o malloc mymalloc.c
clean:
	rm malloc
	rm phase2
