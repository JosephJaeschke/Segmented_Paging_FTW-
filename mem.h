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
	int tid;
	int pageNum;//first,second,... page of the thread so we know where to put it
	int used:1;
} memBook;

//static void handler (int signum, siginfo_t* si, void* unused);

int abs (int a);

char* shalloc (size_t size);

char* myallocate (size_t size, char* file, int line, int type);

void coalesce (char* ptr, int type, int has);

void mydeallocate (char* ptr, char* file, int line, int type);

#endif /* _MEM_H_ */
