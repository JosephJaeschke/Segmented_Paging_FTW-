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

#endif /* _MEM_H_ */
