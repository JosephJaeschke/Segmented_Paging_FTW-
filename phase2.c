#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>
#include "mem.h"

#define malloc(x) myallocate(x,__FILE__,__LINE__,0);
#define free(x) mydeallocate(x,__FILE__,__LINE__,0);
#define VER 987

char* mem;
int init=0;
struct sigaction sa;

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
	if(init==0)
	{
		mem=(char*)memalign(sysconf(_SC_PAGE_SIZE),8000000);
		sa.sa_flags=SA_SIGINFO;
		sigemptyset(&sa.sa_mask);
		sa.sa_sigaction=handler;
		if(sigaction(SIGSEGV,&sa,NULL)==-1)
		{
			printf("Fatal error in signal handler setup\n");
			exit(EXIT_FAILURE);
		}
		memHeader h;
		h.free=0;
		h.next=mem+size;
		h.prev=NULL;
		h.verify=VER;
		memcpy((void*)mem,(void*)&h,sizeof(memHeader));
		memHeader rest;
		rest.free=1;
		rest.next=mem+sizeof(mem); 
		rest.prev=mem;
		rest.verify=VER;
		memcpy((void*)mem+size,(void*)&rest,sizeof(memHeader));
		init=1;
		return mem+sizeof(memHeader);
	}
	//use Best Fit to find free spot
	int best=8000000;
	memHeader* bestBlock=NULL;
	memHeader* headers=(memHeader*)mem;
	while((char*)headers!=mem+sizeof(mem)) 
	{
		if(headers->free!=0)
		{
			int sz=((char*)headers->next)-((char*)headers);//big enough for req size+header
			if(abs(best-size)>abs(sz-size))
			{
				best=sz;
				bestBlock=headers;
			}
		}
		headers=(memHeader*)headers->next;
	}
	if(bestBlock==NULL)
	{
		return NULL;
	}
	memHeader rest;
	bestBlock->free=0;
	char* tempNext=bestBlock->next;
	bestBlock->next=(char*)((void*)bestBlock+size);
	rest.free=1;
	rest.next=tempNext;
	rest.prev=(char*)bestBlock->prev;
	rest.verify=VER;
	memcpy((void*)bestBlock->next,(void*)&rest,sizeof(memHeader));
	//printf("bb ver=%d\n",bestBlock->verify);
	return (char*)((void*)bestBlock+sizeof(memHeader));
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
