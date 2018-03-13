typedef struct memHeader_
{
	short segNum;
	short verify;
	char* next;
	char* prev;
	short free:1;
	int id;
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;
	int used:1;
} memBook;
