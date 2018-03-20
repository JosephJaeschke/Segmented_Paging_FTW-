#ifndef _MEM_H_
#define _MEM_H_


typedef struct memHeader_
{
	short segNum; //don't think this is needed
	short verify;
	char* next;
	char* prev;
	short free:1;
	int id;	//this is not needed
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;//first,second,... page of the thread so we know where to put it
	int used:1;
	short first_in_chain; //whether this is the first page in the list of pages thread is responsible for
	short numPages; //number of pages after that are part of 
	char* next_page; //list of pages this thread is responsible for
} memBook;

#endif /* _MEM_H_ */
