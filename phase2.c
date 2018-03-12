#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>
#include "mem.h"
#include "my_pthread_t.h"

#define malloc(x) myallocate(x,__FILE__,__LINE__,0);
#define free(x) mydeallocate(x,__FILE__,__LINE__,0);
#define VER 987
#define MEM_SIZE 8000000
#define MEM_STRT 2002944 //first page of non-system memory

char* mem;
int meminit=0;
struct sigaction sa;
int id=1; //equivalent to curr->tid
memBook segments[1954];

static void handler(int signum,siginfo_t* si,void* unused)
{
	//printf("Got SIGSEGV @ addr 0x%lx\n",(long)si->si_addr);
	if((char*)si->si_addr>=mem&&(char*)si->si_addr<=mem+MEM_SIZE)
	{
		//in memory so fix it
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
		bzero(mem,MEM_SIZE);
		printf("-0, %lu\n",sizeof(memHeader));	
		sa.sa_flags=SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction=handler;
		if(sigaction(SIGSEGV,&sa,NULL)==-1)
		{
			printf("Fatel error in signal handler setup\n");
			exit(EXIT_FAILURE);
		}
		memHeader h;
		h.free=1;
		h.prev=NULL;
		h.next=mem+size;
		h.verify=VER;
		h.id=id;//CHANGE THIS FOR THREAD ID
		memcpy(mem+MEM_STRT,&h,sizeof(memHeader));
		printf("started initing @ %p and h.id=%d\n",mem+MEM_STRT,h.id);
		memHeader rest;
		rest.free=1;
		rest.prev=mem;
		rest.next=mem+MEM_STRT+_SC_PAGE_SIZE;
		rest.id=id;//CHANGE THIS FOR THREAD ID
		memcpy(mem+MEM_STRT+size-sizeof(memHeader),&rest,sizeof(memHeader));
		meminit=1;
	}
	if(type!=0)
	{
		//non-system request for mem
		//add start index for non-system mem (ie don't start looking at 0)
		memHeader data;
		char* ptr=mem+MEM_STRT;
		printf("start searching @ ptr=%p\n",ptr);
		while(ptr!=mem+MEM_SIZE)
		{
			printf("-");
			fflush(stdout);
			memcpy(&data,ptr,sizeof(memHeader));
			ptr=ptr+_SC_PAGE_SIZE;
			printf("id:%d\n",data.verify);
			if(data.id==id)
			{
				//found its page!!
				printf("-1\n");
				int best=_SC_PAGE_SIZE;
				char* temp=ptr;
				memHeader* bestFit=NULL;
				memHeader* curr=(memHeader*)ptr;
				while((char*)curr!=temp+_SC_PAGE_SIZE)
				{
					curr=(memHeader*)ptr;
					if(curr->free!=0)
					{
						int sz=((char*)curr->next)-((char*)curr);//big enough for req size+header
						if(abs(best-size)>abs(sz-size)&&(sz-size)>=0)
						{
							best=sz;
							bestFit=curr;
						}
					}
					curr=(memHeader*)curr->next;
				}
				if(bestFit==NULL)
				{
					return NULL;
				}
				memHeader rest;
				bestFit->free=0;
				char* tempNext=bestFit->next;
				bestFit->next=(char*)bestFit+size;
				rest.free=1;
				rest.prev=(char*)bestFit;
				rest.next=tempNext;
				rest.verify=VER;
				rest.id=bestFit->id;
				memcpy((void*)(((void*)bestFit)+size),(void*)&rest,sizeof(memHeader));
				return (char*)(((void*)bestFit)+sizeof(memHeader));
			}
			else if(data.id==0)
			{
				printf("2\n");
				//found free page!!
				memHeader new;
				new.free=0;
				new.prev=NULL;
				new.next=ptr+size;
				new.verify=VER;
				new.id=id;//CHANGE FOR THREAD ID
				memcpy((void*)ptr,(void*)&new,sizeof(memHeader));
				memHeader rest;
				rest.free=1;
				rest.prev=ptr;
				rest.next=ptr+_SC_PAGE_SIZE;
				rest.verify=VER;
				rest.id=type;//CHANGE FOR THREAD ID
				memcpy((void*)(ptr+size),(void*)&rest,sizeof(memHeader));
				return ptr+sizeof(memHeader);
			}
			else
			{
		//		printf("-");
				ptr+=_SC_PAGE_SIZE;
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

void coalesce(char* ptr)
{
	printf("-c\n");
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	if(prv==NULL&&(char*)nxt==mem+MEM_SIZE)
	{
		return;
	}
	else if(prv!=NULL&&(char*)nxt!=mem+MEM_SIZE)
	{
		if(prv->free==0&&nxt->free==0)
		{
			return;
		}
		if(nxt->free!=0)
		{
			((memHeader*)ptr)->next=nxt->next;
		}
		if(prv->free!=0)
		{
			prv->next=((memHeader*)ptr)->next;
			nxt->prev=((memHeader*)ptr)->prev;	
			ptr=(char*)prv;
		}
		coalesce(ptr);
	}
	else if((char*)nxt==mem+MEM_SIZE)
	{
		if(prv->free!=0)
		{
			prv->next=((memHeader*)ptr)->next;
			nxt->prev=((memHeader*)ptr)->prev;	
			ptr=(char*)prv;
			coalesce(ptr);
		}
	}
	else if(prv==NULL)
	{
		//breaking here
		if(nxt->free!=0)
		{
			((memHeader*)ptr)->next=nxt->next;
			coalesce(ptr);	
		}
	}
	
}

void mydeallocate(char* ptr,char* file,int line,int type)
{
	memHeader* real=((memHeader*)(ptr-sizeof(memHeader)));
	printf("ver=%d\n",real->verify   );
	printf("in dealloc %p - %p\n", real, real->next);
	
	if (type!=0)
	{
		//non-system request for free
		if(real->verify!=VER)
		{
			printf("ERROR: Not pointing to void addr\n");
			return;
		}
		if(real->id!=id)//will be changed to look at memBook
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
		//don't know if sys is allowed to free usr mem or not??
	}
	return;
}

int main()
{
	short* t=(short*)myallocate(sizeof(short),__FILE__,__LINE__,6);
	//printf("size of short: %d, size of mem: %d\n", sizeof(short), (0xaa-0xa8));
	printf("mem: %p-%p\n",mem,mem+MEM_SIZE);
	printf("Given ptr=%p\n",t);
	short* temp=t;
	short x=888;
	t=&x;
	t=temp;
	mydeallocate((char*)t,__FILE__,__LINE__,6);
}
