#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>
#include <assert.h>
#include <sys/mman.h>
#include "mem.h"
#include "my_pthread_t.h"

#define malloc(x) myallocate(x,__FILE__,__LINE__,1);
#define free(x) mydeallocate(x,__FILE__,__LINE__,1);
#define VER 987
#define MEM_SIZE 8003584
#define MEM_STRT 2002944 //first page offset of non-system memory
#define BOOK_STRT 489 //first page of non-sys memory
#define BOOK_END 1951
#define SHALLOC_STRT 1951
#define SHALLOC_END 1955

char* mem;
int meminit=0;
struct sigaction sa;
int id=1; //equivalent to curr->tid
memBook segments[1955]={0};//change 1953 to sysconf dervived value
//keeps track of which threads are in and which are not along with which segment they are present in.  -1: spot has not been used, -2: vacant but used
//int threadList[1953][2] = { {-1} };



static void handler(int signum,siginfo_t* si,void* unused)
{
	//printf("Got SIGSEGV @ addr 0x%lx\n",(long)si->si_addr);
	char* addr=(char*)si->si_addr;
	if(addr>=mem&&addr<=mem+MEM_SIZE)//is end boundary inclusive?
	{
//		int loc=addr/sysconf(_SC_PAGE_SIZE);	
	}
	else
	{
		//real segfault
		printf("Segmentation Fault (core dumped)\n");
		exit(EXIT_FAILURE); //didn't specify what to do
	}
	return;
}

int abs(int a)
{
	if(a<0)
	{
		return -a;
	}
	return a;
}

char* myallocate(size_t size,char* file,int line,int type)
{
	
	size+=sizeof(memHeader);
	if(meminit==0)
	{
		mem=(char*)memalign(sysconf(_SC_PAGE_SIZE),MEM_SIZE);
		printf("mem:%p - %p\n",mem,mem+MEM_SIZE);
		printf("header size:%lu,0x%X\n",sizeof(memHeader),(int)sizeof(memHeader));	
		sa.sa_flags=SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction=handler;
		if(sigaction(SIGSEGV,&sa,NULL)==-1)
		{
			printf("Fatel error in signal handler setup\n");
			exit(EXIT_FAILURE);
		}
		int i;
		for(i=0;i<SHALLOC_END;i++)
		{
			segments[i].tid=-1;
			segments[i].pageNum=-1;
		}
		meminit=1;
	}
	if(type!=0)
	{
		int a,has=0;
		for(a=BOOK_STRT;a<BOOK_END;a++)
		{
			if(segments[a].tid==id)
			{
				has++;
			}
		}
		if(has>0)
		{
			printf("-1\n");
			//find page it owns that can fit req
			for(a=BOOK_STRT;a<BOOK_END;a++)
			{
				char* ptr=mem+a*sysconf(_SC_PAGE_SIZE);
				if(segments[a].tid==id)//change to curr->id
				{
					//apply best-fit
					int best=sysconf(_SC_PAGE_SIZE);
					int sz;
					char* temp=ptr;
					memHeader* bestFit=NULL;
					memHeader* curr=(memHeader*)ptr;
					while((char*)curr!=mem+(a+1)*sysconf(_SC_PAGE_SIZE))
					{
						if(curr->free!=0)
						{
							sz=((char*)curr->next)-((char*)curr);//big enough for req size+header
							if((abs(best-(signed)size)>abs(sz-(signed)size))&&(sz-(signed)size)>=0)
							{
								best=sz;
								bestFit=curr;
							}
						}
						curr=(memHeader*)curr->next;
					}
					if(bestFit!=NULL)//found a fit, move to appropriate spot and return a ptr
					{
						bestFit->free=0;
						if((best-(signed)size)>sizeof(memHeader))
						{
							memHeader rest;
							rest.id=id; //change for curr->tid
							rest.free=1;
							rest.prev=(char*)bestFit;
							rest.next=bestFit->next;
							rest.verify=VER;
							bestFit->next=(char*)bestFit+size;//!!!
							memcpy((void*)(((char*)bestFit)+size),(void*)&rest,sizeof(memHeader));
						}
						if((a-BOOK_STRT)!=segments[a].pageNum)//move to right spot
						{
							printf("== MOVE ==\n");
							printf("a=%d, pgNum=%d\n",a,segments[a].pageNum);

							int loc=segments[a].pageNum+a;
							memBook temp=segments[a];
							char tempData[sysconf(_SC_PAGE_SIZE)];
							memcpy(tempData,mem+a*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
							//
							segments[a]=segments[loc];
					memcpy(mem+a*sysconf(_SC_PAGE_SIZE),mem+loc*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
							
							segments[loc]=temp;
							memcpy(mem+loc*sysconf(_SC_PAGE_SIZE),tempData,sysconf(_SC_PAGE_SIZE));
							//update bestFit pointer
							//int dist=a-loc;
							//bestFit=bestFit-(*sysconf(_SC_PAGE_SIZE));
						}
						return (char*)bestFit+sizeof(memHeader);
					}


				}
			}
		}
		printf("-2\n");
		//thread has no pages or can't fit req size
		for(a=BOOK_STRT;a<BOOK_END;a++)
		{
			//look for a free page
			if(segments[a].used==0)
			{
				segments[a].used=1;
				segments[a].tid=id;//change to curr->id
				segments[a].pageNum=has;//zeroth page
				memHeader new;
				new.free=0;
				new.prev=NULL;
				new.next=mem+(has+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);//point o next page as if already at spot
				new.verify=VER;
				new.id=id;//CHANGE FOR THREAD ID
				if(sysconf(_SC_PAGE_SIZE)-size>sizeof(memHeader))
				{
					memHeader rest;
					rest.prev=mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
					rest.next=mem+(has+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
					new.next=mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE)+size;
					printf("new.next=%p\n",new.next);
					rest.verify=VER;
					rest.id=id;
					rest.free=1;
					memcpy(mem+a*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
				}
				memcpy(mem+a*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
				//move to first spot for usr mem
				if((a-BOOK_STRT)!=0)
				{
					printf("== MOVE ==\n");
					//store clean mem+memBook
					memBook temp=segments[a];
					char tempData[sysconf(_SC_PAGE_SIZE)];
					memcpy(tempData,mem+a*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
					//move out what was in first spot
					segments[a]=segments[BOOK_STRT];
				memcpy(mem+a*sysconf(_SC_PAGE_SIZE),mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
					//move in clean page
					segments[BOOK_STRT]=temp;
					memcpy(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),tempData,sysconf(_SC_PAGE_SIZE));

				}
				printf("-->%p\n",mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader));
				return mem+(BOOK_STRT)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
			}
		}
		return NULL;
	}
	else
	{
		//system wants mem
		//os mem should go in front of mem array
		//~25-50% of array and should never go in swap
	}
	return NULL;
}

void coalesce(char* ptr)
{
	printf("-coal(%p)\n",ptr);
	int segIndex=(ptr-mem)/sysconf(_SC_PAGE_SIZE);
	char* pgStart=mem+segIndex*sysconf(_SC_PAGE_SIZE);
	char* pgEnd=mem+(segIndex+1)*sysconf(_SC_PAGE_SIZE);
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	if(prv==NULL&&(char*)nxt==pgEnd)
	{
		//empty page
		//mark it as empty
		printf("-a\n");
		segments[segIndex].used=0;
		segments[segIndex].tid=-1;
		segments[segIndex].pageNum=-1;
		return;
	}
	else if(prv==NULL&&(char*)nxt!=pgEnd)//on left boundary of page
	{
		printf("-b and %p!=%p\n",nxt,pgEnd);
		if(nxt->free!=0)
		{
			//recuses infinently here on the deallocate of a from man
	//		printf("f\n");
			((memHeader*)ptr)->next=nxt->next;
			coalesce(ptr);
		}
		return;
			
	}
	else if(prv!=NULL&&(char*)nxt==pgEnd)//on right boundary of page
	{
		printf("-c\n");
		if(prv->free!=0)
		{
			prv->next=((memHeader*)ptr)->next;
			ptr=(char*)prv;
			coalesce(ptr);
		}
		return; 
	}
	else if(prv!=NULL&&(char*)nxt!=pgEnd)//somewhere in the page
	{
		printf("-d\n");
		if(nxt->free!=0||prv->free!=0)
		{
			if(nxt->free!=0)
			{
				printf("d1\n");
				((memHeader*)ptr)->next=nxt->next;
			}
			if(prv->free!=0)
			{
				printf("d2\n");
				prv->next=((memHeader*)ptr)->next;
				nxt->prev=((memHeader*)ptr)->prev;	
				ptr=(char*)prv;
			}
			coalesce(ptr);
		}
		return;
	}
}

void mydeallocate(char* ptr,char* file,int line,int type)
{
	printf("-dealoc\n");
	memHeader* real=((memHeader*)(ptr-sizeof(memHeader)));
	printf("ver=%d\n",real->verify   );
	printf("in dealloc %p - %p\n", real, real->next);
	//printf(">%lu\n",((char*)real-mem)/sysconf(_SC_PAGE_SIZE));
	if (type!=0)
	{
		//non-system request for free
		if(real->verify!=VER)
		{
			printf("ERROR: Not pointing to void addr\n");
			return;
		}
		if(segments[((char*)real-mem)/sysconf(_SC_PAGE_SIZE)].tid!=id)//will be changed to look at memBook
		{
	//		printf("%d=%d 1\n",((memHeader*)ptr)->id,type);
			printf("ERROR: You do not own this memory\n");
			return;
		}
		real->free=1;
		coalesce(ptr-sizeof(memHeader));
	}
	else
	{
		//sys wants to free mem
	}
	ptr=NULL;
	return;
}

int main()
{

	short* t=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	//printf("size of short: %d, size of mem: %d\n", sizeof(short), (0xaa-0xa8));
	printf("mem: %p...|%p-%p\n",mem,mem+MEM_STRT,mem+MEM_SIZE);
//	printf("+%p\n",((memHeader*)((memHeader*)((char*)t-sizeof(memHeader)))->next)->next);
	printf("t Given ptr=%p\n",t);
	short* u=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("u Given ptr=%p\n",u);
	/*
	short* v=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("v Given ptr=%p\n",v);
	mydeallocate((char*)u,__FILE__,__LINE__,6);
	short* a=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("a Given ptr=%p\n",a);

	mydeallocate((char*)t,__FILE__,__LINE__,6);
	printf("=\n");
	mydeallocate((char*)a,__FILE__,__LINE__,6);
	printf("==\n");
	mydeallocate((char*)v,__FILE__,__LINE__,6);
	*/
	printf("---------------------------------\n");
	id=2;
	short* o=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("o Given ptr=%p\n",o);
	//mydeallocate((char*)t,__FILE__,__LINE__,6);
	return 0;
}
