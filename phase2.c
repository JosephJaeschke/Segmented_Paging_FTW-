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

char* mem;
int meminit=0;
struct sigaction sa;
int id=1;


static void handler(int signum,siginfo_t* si,void* unused)
{
	printf("Got SIGSEGV @ addr 0x%lx\n",(long)si->si_addr);
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
		mem=(char*)memalign(sysconf(_SC_PAGE_SIZE),8000000);
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
		h.id=type;//CHANGE THIS FOR THREAD ID
		memcpy((void*)mem,(void*)&h,sizeof(memHeader));
		memHeader rest;
		rest.free=1;
		rest.prev=mem;
		rest.next=mem+_SC_PAGE_SIZE;
		rest.id=type;//CHANGE THIS FOR THREAD ID
		memcpy((void*)mem,(void*)&rest,sizeof(memHeader));
	}
	if(type!=0)
	{
		//non-system request for mem
		//add start index for non-system mem (ie don't start looking at 0)
		char* ptr=mem;
		while(ptr!=mem+sizeof(mem))
		{
			if(((memHeader*)ptr)->id==type)
			{
				//found its page!!
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
			else if(((memHeader*)ptr)->id==0)
			{
				//found free page!!
				memHeader new;
				new.free=0;
				new.prev=NULL;
				new.next=ptr+size;
				new.verify=VER;
				new.id=type;//CHANGE FOR THREAD ID
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
				ptr+=_SC_PAGE_SIZE;
			}
		}
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
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	if(prv==NULL&&(char*)nxt==mem+sizeof(mem))
	{
		return;
	}
	else if(prv!=NULL&&(char*)nxt!=mem+sizeof(mem))
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
	else if((char*)nxt==mem+sizeof(mem))
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
		if(nxt->free!=0)
		{
			((memHeader*)ptr)->next=nxt->next;
			coalesce(ptr);	
		}
	}
	
}

void mydeallocate(char* ptr,char* file,int line,int type)
{
	printf("ver=%d\n",((memHeader*)((void*)ptr-sizeof(memHeader)))->verify   );
	//printf("in dealloc %p\n",((memHeader*)((void*)ptr-sizeof(memHeader)))->next);
	if(((memHeader*)((void*)ptr-sizeof(memHeader)))->verify!=VER)
	{
		printf("ERROR: Not pointing to void addr\n");
		return;
	}
	if(((memHeader*)((void*)ptr-sizeof(memHeader)))->id!=type)//change for TID
	{
		printf("ERROR: You do not own this memory\n");
		return;
	}
	((memHeader*)((void*)ptr-sizeof(memHeader)))->free=1;
	coalesce(((char*)(void*)ptr-sizeof(memHeader)));
	return;
}

int main()
{
	printf("mem: %p-%p\n",mem,mem+sizeof(mem));
	short* t=(short*)malloc(sizeof(short));
	short* temp=t;
	short x=888;
	t=&x;
	printf("Here it is:%d\n",*t);
	t=temp;
	free((char*)t);
}
