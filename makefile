all:
	gcc -g -o phase2 phase2.c
malloc:
	gcc -Wall -g -o malloc mymalloc.c
v:
	gcc -g -o virtual virtual.c
m:
	gcc -g -o multi multi.c
pthread: my_pthread.c multi.c
	gcc -Wall -o pthread my_pthread.c multi.c
s:
	gcc -g -o swap swap.c
clean:
	rm swap
	rm malloc
	rm phase2
