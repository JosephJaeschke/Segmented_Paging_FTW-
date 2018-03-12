typedef struct memHeader_
{
	short segNum;
	short verify;
	char* next;
	char* prev;
	int free:1;
	int id;
} memHeader;

typedef struct memBook_
{
	int tid;
	int pageNum;
	int free:1;
} memBook;
