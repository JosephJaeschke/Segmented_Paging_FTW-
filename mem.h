typedef struct memHeader_
{
	short segNum;
	short verify;
	char* next;
	char* prev;
	short free:1;
	int id;
	struct memHeader_* next_page;
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;
	int used:1;
} memBook;
