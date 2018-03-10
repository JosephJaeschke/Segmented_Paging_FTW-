typedef struct memHeader_
{
	int tid;
	int pageNum;
	short verify;
	char* next;
	char* prev;
	int free:1;
	int id;
} memHeader;

typedef struct memBlock_
{
	int tid;
	int pageNum;
	int free:1;
} memBlock;
