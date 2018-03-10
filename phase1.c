#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "mem.h"

#define VER 987

//obligatory test comment to make sure I can use git correctly
//I DID ANOTHER THING!!

char mem[8000000];
int init=0;
memHeader m;

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
		printf("mem: %p-%p\n",mem,mem+sizeof(mem));
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
		m=rest;
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
	printf("-in coal\n");
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	printf("prv:%p\n",(((memHeader*)ptr)->prev));
	fflush(stdout);
	if(prv==NULL&&(char*)nxt==mem+sizeof(mem))
	{
		return;
	}
	else if(prv!=NULL&&(char*)nxt!=mem+sizeof(mem))
	{
		printf("--i\n");
		if(prv->free==0&&nxt->free==0)
		{
			return;
		}
		printf("--i1\n");
		fflush(stdout);
		if(nxt->free!=0)
		{
			((memHeader*)ptr)->next=nxt->next;
		}
		printf("--i2\n");
		fflush(stdout);
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
		printf("--j\n");
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
		printf("--k %p\n",nxt);
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
	}
	((memHeader*)((void*)ptr-sizeof(memHeader)))->free=1;
	coalesce(((char*)(void*)ptr-sizeof(memHeader)));
	return;
}

int main()
{
	short* t=(short*)myallocate(sizeof(short),__FILE__,__LINE__,0);
	printf("alloced 1\n");
	short* s=(short*)myallocate(sizeof(short),__FILE__,__LINE__,0);
	printf("alloced 2\n");
	short* sh=s;
	*sh=8;
	printf("*sh=%d\n",*sh);
	short* p=(short*)myallocate(sizeof(short),__FILE__,__LINE__,0);
	printf("alloced 3\n");
	mydeallocate((char*)t,__FILE__,__LINE__,0);
	printf("-\n");
	printf("2 prev after dealloc %p\n",((memHeader*)((void*)s-sizeof(memHeader)))->prev);
	fflush(stdout);
	mydeallocate((char*)s,__FILE__,__LINE__,0);
	printf("--\n");
	fflush(stdout);
	mydeallocate((char*)p,__FILE__,__LINE__,0);
	printf("---\n");
	printf("%p\n",((memHeader*)mem)->next);
	fflush(stdout);
}
