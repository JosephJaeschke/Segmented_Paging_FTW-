typedef struct memHeader_
{
	short verify;
	char* next;
	char* prev;
	int free:1;
} memHeader;

typedef struct memBlock_
{
	int tid;
	int pageNum;
	int free:1;
} memBlock;
