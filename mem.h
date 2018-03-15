typedef struct memHeader_
{
	short segNum;
	short verify;
	char* next;
	char* prev;
	short free:1;
	int id;
	struct memHeader_* next_page;	//list of pages this thread is responsible for
	size_t mem_left; //used to tell how much total memory is left in a page 
	//still deciding if it's a good idea to keep here or membook
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;
	int used:1;
} memBook;

