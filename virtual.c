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
#define SYS_BOOK_END 489 //not inclusive
#define MEM_STRT 2002944 //first page offset (in bytes) of non-system memory
#define BOOK_STRT 489 //first page of non-sys memory
#define BOOK_END 1951 //not inclusive
#define SHALLOC_STRT 1951
#define SHALLOC_END 1955 //not inclusive

char* mem;
int meminit=0;
struct sigaction sa;
int id=1; //equivalent to curr->tid
memBook segments[1953]={0};//change 1953 to sysconf dervived value
//keeps track of which threads are in and which are not along with which segment they are present in.  -1: spot has not been used, -2: vacant but used
int threadList[1953][2] = { {-1} };



static void handler(int signum,siginfo_t* si,void* unused)
{
	//printf("Got SIGSEGV @ addr 0x%lx\n",(long)si->si_addr);
	if((char*)si->si_addr>=mem&&(char*)si->si_addr<=mem+MEM_SIZE)
	{
		//in mem so fix it
		printf("I did this\n");
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
		memset(mem,0,MEM_SIZE);
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
		for(i=0;i<1953;i++)
		{
			segments[i].mem_left=sysconf(_SC_PAGE_SIZE);
		}
		meminit=1;
	}
	if(type!=0)
	{
		if(segments[MEM_STRT].tid!=id)//change for curr->id
		{
			//different thread wants mem
			//move one of its pages to right spot and allocate from there
			int i;
			short has=0;//how many the thread owns
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				if(segments[i].tid==id) //chane to curr->tid
				{
					has++;
				}
			}
			if(has==0)
			{
				//first malloc,find clean page
				for(i=BOOK_STRT;i<BOOK_END;i++)
				{
					if(segments[i].used==0)
					{
						break;
					}
				}
				if(i==BOOK_END)
				{
					//no free pages
					return NULL;
				}
				memBook temp=segments[BOOK_STRT];
				char dataTemp[sysconf(_SC_PAGE_SIZE)];
				memcpy(dataTemp,mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[BOOK_STRT]=segments[i];
				memcpy(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),mem+i*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[i]=temp;
				memcpy(mem+i*sysconf(_SC_PAGE_SIZE),dataTemp,sysconf(_SC_PAGE_SIZE));

			}
			//see if it has any pages already that can fir reg
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				if(segments[i].tid==id)//change to curr->id
				{
					//check if enough space in page

				}
			}
			if(i==BOOK_END)
			{
				//thread has no pages, get a fresh one and allocate from MEM_STRT
			}

		}
		if(size>sysconf(_SC_PAGE_SIZE))
		{
			//request more memory than is in one page
			double  number=size/sysconf(_SC_PAGE_SIZE);
			int tempSize=(signed)size;
			if(number-(int)number!=0)
			{
				number++;
			}
			int pgReq=(int)number;
			int i;
			int pgCount=0;
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				//this looks for contiguous pages
				if(segments[i].used==0)
				{
					pgCount++;
				}
				else
				{
					pgCount=0;
				}
				if(pgCount==pgReq)
				{
					break;
				}
			}
			if(i==BOOK_END+1)
			{
				//could not find enough space
				return NULL;
			}
			i=i-pgCount+1; //find first page
			int j,memUsed=0;
			for(j=0;j<pgReq;j++)
			{
				segments[j].tid=id;//change to curr->id
				segments[j].used=1;
				if(tempSize>sysconf(_SC_PAGE_SIZE))
				{
					memUsed=sysconf(_SC_PAGE_SIZE);
					tempSize=tempSize-sysconf(_SC_PAGE_SIZE);
				}
				else
				{
					memUsed=tempSize;//on last page
				}
				segments[j].mem_left=memUsed;
			}
			memHeader new;
			new.id=id;//change to curr->id
			new.verify=VER;
			new.prev=NULL;
			new.next=mem+(i+pgCount)*sysconf(_SC_PAGE_SIZE)+(size%sysconf(_SC_PAGE_SIZE));
			new.free=0;
			new.segNum=i;
			segments[i].first_in_chain=1;
			segments[i].next_page=mem+sysconf(_SC_PAGE_SIZE);
			int k;
			for(k=0;k<1;k++)
			{
				
			}
			if(segments[j-1].mem_left>sizeof(memHeader));
			{
				new.next=mem+(i+pgCount-1)*sysconf(_SC_PAGE_SIZE)+tempSize;
				memHeader rest;
				rest.id=id;//change to curr->id
				rest.verify=VER;
				rest.free=1;
				rest.segNum=i+pgCount-1;
				rest.next=mem+(i+pgCount)*sysconf(_SC_PAGE_SIZE);
				rest.prev=mem+i*sysconf(_SC_PAGE_SIZE);
				segments[i].first_in_chain=0;
				memcpy(mem+(i+pgCount-1)*sysconf(_SC_PAGE_SIZE)+tempSize,&rest,sizeof(memHeader));
			}
			memcpy(mem+i*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
		}
	
		//memHeader data;
		//char* ptr=mem+MEM_STRT;
		//printf("start searching @ ptr=%p\n",ptr);
		//printf("end @ ptr=%p\n",mem+MEM_SIZE);
		int i;
		//memHeader* 	extra_page 	= NULL; //used to determine whether we need an extra page
		//todo: handle requests for memory larger than a single page 
		//check if it's in the list

		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			//printf("-");
			//printf("id:%d\n",data.free);
			//i = (seg_pos != -1) ? seg_pos : i;
			char* ptr=mem+i*sysconf(_SC_PAGE_SIZE);
			//char* ptr = (seg_pos == -1) ? mem + i*sysconf(_SC_PAGE_SIZE) : mem + seg_pos*sysconf(_SC_PAGE_SIZE);
			
			if(segments[i].tid==id)
			{
					//found its page!!
					printf("-1\n");
					int best=sysconf(_SC_PAGE_SIZE);
					char* temp=ptr;
					int sz;
					memHeader* bestFit=NULL;
					memHeader* curr=(memHeader*)ptr;
					while((char*)curr<mem+(i+1)*sysconf(_SC_PAGE_SIZE))
					{
						if(curr->free!=0)
						{
							sz=((char*)curr->next)-((char*)curr);//big enough for req size+header
							printf("->%d, %d\n",sz,sz-(signed)size);
							if((abs(best-(signed)size)>abs(sz-(signed)size))&&(sz-(signed)size)>=0)
							{
								printf("-update\n");
								best=sz;
								bestFit=curr;
							}
						}
						curr=(memHeader*)curr->next;
					}
					
					//case where there isn't enough continguous memory in this segment for a best fit
					//we'll just look for another page
					printf("-->%d\n",best);
					if(bestFit==NULL)
					{
						//added break to go to next for loop looking for empty pages

						//printf("null\n");
						//return NULL;
						//extra_page = (memHeader*) ptr;
						//i = BOOK_STRT;
						//seg_pos = -1;
						//continue;
						break;
					}
				
					bestFit->free=0;
					//don't need to do rest b/c it used to be rest or a filled out block i think
					if((best-(signed)size)>(sizeof(memHeader)))
					{
						memHeader rest;
						rest.id=id; //change for curr->tid
						rest.free=1;
						rest.prev=(char*)bestFit;
						rest.next=bestFit->next;
						rest.segNum = i;
						rest.verify=VER;
						segments[i].first_in_chain=0;
						segments[i].next_page = NULL;
						bestFit->next=(char*)bestFit+size;
						memcpy((void*)(((char*)bestFit)+size),(void*)&rest,sizeof(memHeader));
					}

					segments[i].mem_left -= size;
					return (char*)(((char*)bestFit)+sizeof(memHeader));
			}
		}
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			char* ptr=mem+i*sysconf(_SC_PAGE_SIZE);
			if(segments[i].used==0)
			{
				//found free page!!
				printf("2\n");
				segments[i].used=1;
				segments[i].tid=id;//change to proper tid
				segments[i].mem_left -= size;
				memHeader new;
				new.free=0;
				new.prev=NULL;
				new.next=mem+(i+1)*sysconf(_SC_PAGE_SIZE);//////////////////////////////////////////////////////////
				new.verify=VER;
				new.id=id;//CHANGE FOR THREAD ID
				new.segNum = i; //important that all memheaders that start at pages have a valid segnum
				segments[i].first_in_chain=0;
				segments[i].next_page = NULL;
				if((segments[i].mem_left-(signed)size)>sizeof(memHeader))
				{
					new.next=ptr+(signed)size;
					memHeader rest;
					rest.free=1;
					rest.prev=ptr;
					rest.next=ptr+sysconf(_SC_PAGE_SIZE);
					rest.verify=VER;
					rest.id=id;//CHANGE FOR THREAD ID
					rest.segNum=i;
					segments[i].first_in_chain=0;
					segments[i].next_page=NULL;
					segments[i].mem_left -= sizeof(memHeader);
					memcpy((void*)(ptr+(signed)size),(void*)&rest,sizeof(memHeader));
				}
				memcpy((void*)ptr,(void*)&new,sizeof(memHeader));
				return ptr+sizeof(memHeader);
			}
		}
		//printf("3\n");
		//no free pages
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

void add_thread(short segNum)
{
	int		i;
	for (i = 0; i < 1953 && !( (threadList[i][0] == -1)  || (threadList[i][0] == -2) ); i++);
	
	//case: no spot found
	if (i == 1953) return;
	
	threadList[i][0] = id;
	threadList[i][1] = segNum;
	return; 
}

void remove_thread()
{
	int 		i;
	for (i = 0; i < 1953 && threadList[i][0] == id; i++);
	
	//case: thread not found
	if (i == 1953) 
	{
		printf("Thread with id:%d not found in list.\n", id);
		return;
	}
	
	threadList[i][0] = -2;
	return;
}
/*
memHeader* possible_page(memHeader* start, size_t target)
{
	if (start->page_info.has_info) //extra check that we are at the start of the page
	{	
		while ( (segments[start->segNum].mem_left < target) && (start != NULL) ) start = start->page_info.next_page;
	}
	return start;
}
*/
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
		return;
	}
	else if(prv==NULL&&(char*)nxt!=pgEnd)//on left boundary of page
	{
		printf("-b and %p!=%p\n",nxt,pgEnd);
		if(nxt->free!=0)
		{
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
		//bzero(ptr, ((((memHeader*)(ptr-sizeof(memHeader)))->next) - ptr)),
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
	short* v=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("v Given ptr=%p\n",v);
	//mydeallocate((char*)u,__FILE__,__LINE__,6);
	id=2;
	//mprotect(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),(BOOK_END-BOOK_STRT)*sysconf(_SC_PAGE_SIZE),PROT_NONE);
	short* a=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	printf("a Given ptr=%p\n",a);

	//mydeallocate((char*)t,__FILE__,__LINE__,6);
	//printf("=\n");
	//mydeallocate((char*)a,__FILE__,__LINE__,6);
	//printf("==\n");
	//mydeallocate((char*)v,__FILE__,__LINE__,6);
	//mydeallocate((char*)a,__FILE__,__LINE__,6);
	return 0;
}
