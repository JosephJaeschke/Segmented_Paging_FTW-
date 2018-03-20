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

typedef struct tester_
{
	int a;
	char b;

} tester;

char* mem;
int meminit=0;
struct sigaction sa;
int id=1; //equivalent to curr->tid
memBook segments[1955]={0};//change 1953 to sysconf dervived value
//keeps track of which threads are in and which are not along with which segment they are present in.  -1: spot has not been used, -2: vacant but used
//int threadList[1953][2] = { {-1} };



static void handler(int signum,siginfo_t* si,void* unused)
{
	char* addr=(char*)si->si_addr;
	//assuming sys is not protected (since always loaded and will slow sched if protected)
	if(addr>=mem&&addr<=mem+MEM_SIZE)//is end boundary inclusive?
	{
		printf("(sh) my bad...\n");
		fflush(stdout);
		int loc=(addr-mem)/sysconf(_SC_PAGE_SIZE);//page number of fault	
		int find=loc-BOOK_STRT;//which of its pages thread wanted
		if(segments[loc].tid==id&&segments[loc].pageNum==find)//page is already there
		{
			if(mprotect(mem+loc*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE),PROT_READ|PROT_WRITE)==-1)
			{
				printf("ERROR: Memory protection failure\n");
				exit(1);
			}
			return;
		}
		//printf("(sh) %d!=%d and %d!=%d\n",segments[loc].tid,id,segments[loc].pageNum,find);
		int i;
		if(mprotect(mem,MEM_SIZE,PROT_READ|PROT_WRITE)==-1)
		{
			printf("ERROR: Memory protection failure\n");
			exit(1);
		}
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			if(segments[i].tid==id&&segments[i].pageNum==find)
			{
				//printf("(sh) found it b/c id=%d\n",segments[i].tid);
				//move whats in i to loc
				memBook temp=segments[i];
				char dataTemp[sysconf(_SC_PAGE_SIZE)];
				memcpy(dataTemp,mem+i*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[i]=segments[loc];
				memcpy(mem+i*sysconf(_SC_PAGE_SIZE),mem+loc*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[loc]=temp;
				memcpy(mem+loc*sysconf(_SC_PAGE_SIZE),dataTemp,sysconf(_SC_PAGE_SIZE));
				if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
				{
					printf("ERROR: Memory protection failure\n");
					exit(1);
				}
				if(mprotect(mem+find*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE),PROT_READ|PROT_WRITE)==-1)
				{
					printf("ERROR: Memory protection failure\n");
					exit(1);
				}//i think this speeds things up
				return;
			}
		}
		//couldn't find page, handle as seg fault?
		printf("(sh) Segmentation fault (couldn't find page)\n");
		exit(EXIT_FAILURE);
	}
	else
	{
		//real segfault
		printf("(sh) Segmentation Fault (this was real)\n");
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
			segments[i].first_in_chain=-1; 
			segments[i].numPages=0;
		}
		meminit=1;
	}
	if(mprotect(mem,MEM_SIZE,PROT_READ|PROT_WRITE)==-1)
	{
		printf("ERROR: Memory protection failure\n");
		exit(EXIT_FAILURE);
	}
	if(type!=0)
	{
		if(size>sysconf(_SC_PAGE_SIZE))
		{
			double num=(double)size/sysconf(_SC_PAGE_SIZE);
			//printf("raw num=%f\n",num);
			int tempSize=(signed)size;
			if(num-(int)num!=0)
			{
				num++;
			}
			//find free pages
			int pgReq=(int)num;
			//printf("I NEED %d PAGES\n",pgReq);
			int pgList[pgReq];//list of free pages to use
			int pgCount=0;
			int i;
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				//look for pages that can satisfy request
				if(segments[i].used==0)
				{
			//		printf("pg %d is free\n",i);
					pgList[pgCount]=i;
					pgCount++;
				}
				if(pgCount==pgReq)
				{
					break;
				}
			}
			if(pgCount!=pgReq)
			{
				//not enough free pages
				if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
				{
					printf("ERROR: Memory protection failure\n");
					exit(1);
				}
				return NULL;
			}
			//set the metadata
			//printf("using pages: ");
			//for(i=0;i<pgReq;i++)
			//{
			//	printf("%d, ",pgList[i]);
			//}
			//printf("\b\n");
			int has=0; //maybe just keep a running count in an array
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				//looping through to count pages owned by current thread
				if(segments[i].tid==id)//change to curr->id
				{
					has++;
				}
			}
			//printf("has=%d\n",has);
			for(i=0;i<pgCount;i++)
			{
				//set each of the memBook entries for the new pages
				segments[pgList[i]].used=1;
				segments[pgList[i]].tid=id;//change to curr->id
				segments[pgList[i]].pageNum=pgCount-i; //how many pages are after it for this chunk
				segments[pgList[i]].pageNum=has+i; //where each page will belong when loaded properly
				//printf("pgList[i]=%d\n",pgList[i]);
				//printf("pgList[i].numPages=%d\n",segments[pgList[i]].numPages);
				if(i==0)
				{
					segments[pgList[i]].first_in_chain=1;
				}
				else
				{
					segments[pgList[i]].first_in_chain=2;//dependant
				}

			}
			segments[pgList[0]].numPages=pgReq;
			//make a header for the first page (only first page gets one)
			memHeader new;
			new.id=id;//change to curr->id
			new.verify=VER;
			new.prev=NULL;
			new.next=mem+(has+pgReq+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);//when everything is loaded properly
			new.free=0;
			if((pgReq*sysconf(_SC_PAGE_SIZE))-size>sizeof(memHeader))
			{
				//if there is enough room for more data
				//however, rest will never get used w/ current implementation, so idk
				memHeader rest;
				rest.id=id;//change to curr->id
				rest.verify=VER;
				rest.prev=mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
				new.next=mem+(has+BOOK_STRT+pgReq-1)*sysconf(_SC_PAGE_SIZE)+(size%sysconf(_SC_PAGE_SIZE));
				rest.next=mem+(has+pgReq+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
				rest.free=1;
				memcpy(mem+(pgList[pgReq-1])*sysconf(_SC_PAGE_SIZE),&rest,sizeof(memHeader));
			}
			memcpy(mem+(pgList[0])*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
			//move each page to right spot
			for(i=0;i<pgCount;i++)
			{
				if(pgList[i]==BOOK_STRT+has+i)
				{
					printf("-skip\n");
					continue;
				}
				memBook temp=segments[pgList[i]];
				char* dataTemp[sysconf(_SC_PAGE_SIZE)];
				memcpy(dataTemp,mem+(pgList[i])*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[pgList[i]]=segments[BOOK_STRT+has+i];
		memcpy(mem+(pgList[i])*sysconf(_SC_PAGE_SIZE),mem+(BOOK_STRT+has+i)*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[BOOK_STRT+has+i]=temp;
				memcpy(mem+(BOOK_STRT+has+i)*sysconf(_SC_PAGE_SIZE),dataTemp,sysconf(_SC_PAGE_SIZE));
			}
			if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
			{
				printf("ERROR: Memory protection failure\n");
				exit(1);
			}
			return mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);


		}
		//if req size is larger than page and not enough pages left, above will return NULL before this point
		//req for less than or equal to a page
		int a,has=0;
		for(a=BOOK_STRT;a<BOOK_END;a++)
		{
			if(segments[a].tid==id) //change to curr->tid
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
				printf("$ owner:%d\n",segments[a].tid);
				char* ptr=mem+a*sysconf(_SC_PAGE_SIZE);
				if(segments[a].tid==id)//change to curr->id
				{
					printf("-found @ pg %d\n",a);
					//apply best-fit
					int best=sysconf(_SC_PAGE_SIZE);
					int sz;
					char* was=mem+(segments[a].pageNum+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);//where it should be
					int offset=was-(mem+a*sysconf(_SC_PAGE_SIZE));//how far into the page we are
					//printf("offset:%d\n",offset);
					memHeader* toSet=NULL; //current loc in mem so data can be set with this pointer
					memHeader* bestFit=NULL; //bestFit as if mem was loaded in correct page place
					memHeader* curr=(memHeader*)ptr; //points to curr loc in mem, NOT WHERE THE PAGE THINKS IT IS
					//printf("pgNUM=%d\n",segments[a].pageNum);
					//printf("%p!=%p\n",was,mem+(segments[a].pageNum+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE));
					while(was!=mem+(segments[a].pageNum+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE))
					{
						printf("cur=%p\n",curr);
						printf("!\n");
						if(curr->free!=0)
						{
							sz=((char*)curr->next)-((char*)was);//big enough for req size+header
							printf("->%d, %d\n",sz,sz-(signed)size);
							if((abs(best-(signed)size)>abs(sz-(signed)size))&&(sz-(signed)size)>=0)
							{
								printf("-update\n");
								best=sz;
								if(offset!=0)
								{
									bestFit=(memHeader*)was;
								}
								else
								{
									bestFit=curr;
								}
								toSet=curr;
							}
						}
						//convention: set next/prev as if page was in right spot
						char* temp=was;
						was=curr->next;
						printf("diff=%d\n",(signed)(curr->next-temp));
						printf("c.n=%p, t=%p\n",curr->next,temp);
						curr=(memHeader*)((char*)curr+((signed)(curr->next-temp)));
					}
					printf("-->%d\n",best);
					printf("bf: %p\n",bestFit);
					if(bestFit!=NULL)//found a fit, move to appropriate spot and return a ptr
					{
						printf("-found a fit\n");
						toSet->free=0;
						if((best-(signed)size)>sizeof(memHeader))
						{
							memHeader rest;
							rest.id=id; //change for curr->tid
							rest.free=1;
							rest.prev=(char*)bestFit;
							rest.next=(char*)bestFit->next;
							rest.verify=VER;
							printf("setting next to %p\n",((char*)bestFit)+size);
							toSet->next=((char*)bestFit)+size;//!!!
							memcpy((void*)(((char*)toSet)+size),(void*)&rest,sizeof(memHeader));
						}
						if((a-BOOK_STRT)!=segments[a].pageNum)//move to right spot
						{
							printf("== MOVE ==\n");
							printf("a=%d, pgNum=%d\n",a,segments[a].pageNum);

							int loc=segments[a].pageNum+BOOK_STRT;
							memBook temp=segments[a];
							char tempData[sysconf(_SC_PAGE_SIZE)];
							memcpy(tempData,mem+a*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
							//
							segments[a]=segments[loc];
					memcpy(mem+a*sysconf(_SC_PAGE_SIZE),mem+loc*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
							
							segments[loc]=temp;
							memcpy(mem+loc*sysconf(_SC_PAGE_SIZE),tempData,sysconf(_SC_PAGE_SIZE));
						}
						if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
						{
							printf("ERROR: Memory protection problem\n");
							exit(1);
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
				printf("found a clean page @ %d\n",a);
				segments[a].used=1;
				segments[a].tid=id;//change to curr->id
				segments[a].pageNum=has;
				segments[a].first_in_chain=0;
				segments[a].numPages=0;
				memHeader new;
				new.free=0;
				new.prev=NULL;
				printf("new.next=%p\n",mem+(has+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE));
				new.next=mem+(has+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
				new.verify=VER;
				new.id=id;//CHANGE FOR THREAD ID
				if(sysconf(_SC_PAGE_SIZE)-size>sizeof(memHeader))
				{
					memHeader rest;
					rest.prev=mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
					new.next=mem+(has+BOOK_STRT)*sysconf(_SC_PAGE_SIZE)+size;
					rest.next=mem+(has+1+BOOK_STRT)*sysconf(_SC_PAGE_SIZE);
				//	printf("new.next=%p\n",new.next);
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
					segments[a]=segments[BOOK_STRT+has];
			memcpy(mem+a*sysconf(_SC_PAGE_SIZE),mem+has+BOOK_STRT*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
					//move in clean page
					segments[BOOK_STRT+has]=temp;
					memcpy(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE),tempData,sysconf(_SC_PAGE_SIZE));

				}
				if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
				{
					printf("ERROR: Memory protection failure\n");
					exit(1);
				}
				return mem+(BOOK_STRT)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
			}
		}
		if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
		{
			printf("ERROR: Memory protection failure\n");
			exit(1);
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
	if(segments[segIndex].first_in_chain!=0)
	{
		printf("sI=%d\n",segIndex);
		int i;
		int tempPages=segments[segIndex].numPages;
		printf("tP=%d\n",tempPages);
		for(i=0;i<tempPages;i++)
		{
			segments[segIndex+i].used=0;
			segments[segIndex+i].tid=-1;
			segments[segIndex+i].pageNum=-1;
			segments[segIndex+i].first_in_chain=0;
			segments[segIndex+i].numPages=0;
		}
		return;

	}
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
	if(1)//i don't think it matters if its sys mem or not
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
	}
	ptr=NULL;
	if(mprotect(mem,MEM_SIZE,PROT_NONE)==-1)
	{
		printf("ERROR: Memory protection failure\n");
		exit(1);
	}
	return;
}

int main()
{

	char* t=myallocate(5000,__FILE__,__LINE__,6);
	printf("mem: %p...|%p-%p\n",mem,mem+MEM_STRT,mem+MEM_SIZE);
//	printf("sizeof(tester)=%lu\n",sizeof(tester));
//	printf("+%p\n",((memHeader*)((memHeader*)((char*)t-sizeof(memHeader)))->next)->next);
	printf("t Given ptr=%p\n",t);
	int i;
	for(i=0;i<5000;i++)
	{
		t[i]='d';
	}
	t[4999]='\0';
	printf("t string:%s\n",t);
	mydeallocate(t,__FILE__,__LINE__,6);
	char* hey=myallocate(sizeof(char),__FILE__,__LINE__,6);
	printf("hey given ptr=%p\n",hey);
	return 0;
}
