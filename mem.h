#ifndef _MEM_H_
#define _MEM_H_


typedef struct memHeader_
{
	short segNum; //ASSUMPTION: all memHeaders that start at a page have a valid segNum. in fact, ALL memHeaders will have their segNum
	short verify;
	char* next;
	char* prev;
	short free:1;
	int id;
	struct pageInfo_
	{
		short has_info:1; //info as to whether a memHeader holds info for the whole page, will always be set
		int id;
		short first_in_chain:1; //whether this is the first page in the list of pages thread is responsible for
		struct memHeader_* next_page; //list of pages this thread is responsible for
	} page_info;
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;
	int used:1;
	size_t mem_left; //accessed from memHeader via segments[i].mem_left. _SC_PAGE_SIZE - sizeof(memHeader) is max size of a segment whose used == 1
} memBook;


/* adds threads name and segment number to a list of threads using dynamic alloation*/
void add_thread(short segNum);

/* deletes thread entry from threads using malloc, replaces tid with -2 (meaning used but empty)*/
void remove_thread();

/* returns next page that can fit request.  if none exist, returns NULL 
 * NOTE: does not check if page has large enough contiguous memory, just if it has enough*/
memHeader* possible_page(memHeader* start, size_t target);


#endif /* _MEM_H_ */
