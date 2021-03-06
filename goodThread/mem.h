#ifndef _MEM_H_
#define _MEM_H_


typedef struct memHeader_
{
	short verify;
	char* next;
	char* prev;
	short free:1;
} memHeader;

typedef struct memBook_
{
	my_pthread_t tid;
	int pageNum;//first,second,... page of the thread so we know where to put it
	int used:1;
} memBook;

//static void handler (int signum, siginfo_t* si, void* unused);

int abs (int a);

void* shalloc (size_t size);

void* myallocate (size_t size, char* file, int line, int type);

void coalesce (char* ptr, int type, int has);

void mydeallocate (void* ptr, char* file, int line, int type);

#endif /* _MEM_H_ */
