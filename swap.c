#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <malloc.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include "mem.h"

#define malloc(x) myallocate(x,__FILE__,__LINE__,1);
#define free(x) mydeallocate(x,__FILE__,__LINE__,1);
#define VER 987
#define MEM_SIZE 8388608 //8MB = 2^23 (2048 pgs)
#define MEM_STRT 2510848 //first page offset of non-system memory
#define BOOK_STRT 613 //first page of non-sys memory (512 pages for stacks, 101 for ret vals, queue, etc) (0-612)
#define BOOK_END 2044 //usr gets pgs 613-2043 (1430 pgs)
#define SHALLOC_STRT 2044//shalloc is 4
#define SHALLOC_END 2048
#define MEM_PROT 5861376//8372224 //don't want to mprotect shalloc region

#define SWAP_SIZE 1677216 //16MB
#define SWAP_END 4096

typedef struct tester_
{
	int a;
	char b;

} tester;

char* mem;
int meminit=0;
int sysinit=0;
int shinit=0;
struct sigaction sa;
int id=1; //equivalent to curr->tid
memBook segments[2048]={0};
memBook swap[4096]={0};

int swapfd;

static void handler(int signum,siginfo_t* si,void* unused)
{
	char* addr=(char*)si->si_addr;
	//assuming sys is not protected (since always loaded and will slow sched if protected)
	if(addr>=mem&&addr<=mem+MEM_SIZE)
	{
		//printf("(sh) my bad...\n");
		fflush(stdout);
		int loc=(addr-mem)/sysconf(_SC_PAGE_SIZE);//page number of fault	
		int find=loc-BOOK_STRT;//which of its pages thread wanted
		if(segments[loc].tid==id&&segments[loc].pageNum==find)//page is already there
		{
			if(mprotect(mem+loc*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE),PROT_READ|PROT_WRITE)==-1)
			{
				//unprotect place
				perror("ERROR: ");
				exit(1);
			}
			return;
		}
		//printf("(sh) %d!=%d and %d!=%d\n",segments[loc].tid,id,segments[loc].pageNum,find);
		int i;
		if(mprotect(mem,MEM_PROT,PROT_READ|PROT_WRITE)==-1)
		{
			perror("ERROR: ");
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
				if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)//protect everything
				{
					perror("ERROR: ");
					exit(1);
				}
				if(mprotect(mem+find*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE),PROT_READ|PROT_WRITE)==-1)
				{
					perror("ERROR: ");
					exit(1);
				}
				return;
			}
		}
		//look in swap for it
		for(i=0;i<SWAP_END;i++)
		{
			if(swap[i].tid==id&&swap[i].pageNum==find)
			{
				//find a page to evict
				//naive and choose the one in its spot
				memBook evict=segments[find];
				char evictData[sysconf(_SC_PAGE_SIZE)];
				memcpy(evictData,mem+find*sysconf(_SC_PAGE_SIZE),sysconf(_SC_PAGE_SIZE));
				segments[find]=swap[i];
				char swapData[sysconf(_SC_PAGE_SIZE)];
				if(lseek(swapfd,i*sysconf(_SC_PAGE_SIZE),SEEK_SET)<0)
				{
					perror("ERROR: ");
					exit(1);
				}
				//read swap data
				int numRead=0,curRead=0;
				while(numRead<sysconf(_SC_PAGE_SIZE))
				{
					curRead=read(swapfd,swapData+numRead,sysconf(_SC_PAGE_SIZE)-numRead);
					if(curRead<0)
					{
						continue;
					}
					numRead+=curRead;
				}
				//put swap contents in mem
				memcpy(mem+find*sysconf(_SC_PAGE_SIZE),swapData,sysconf(_SC_PAGE_SIZE));
				//put mem contents in swap
				int numWrite=0,curWrite=0;
				while(numWrite<sysconf(_SC_PAGE_SIZE))
				{
					curWrite=write(swapfd,evictData+numWrite,sysconf(_SC_PAGE_SIZE)-numWrite);
					if(curWrite<0)
					{
						continue;
					}
					numWrite+=curWrite;
				}
				//update metadata
				swap[i]=evict;
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

char* shalloc(size_t size)
{
	size+=sizeof(memHeader);
	if(size+sizeof(memHeader)>4*sysconf(_SC_PAGE_SIZE))
	{
		//too big for shalloc region
		return NULL;
	}
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
			perror("ERROR: ");
			exit(EXIT_FAILURE);
		}
		int i;
		for(i=0;i<SHALLOC_END;i++)
		{
			segments[i].tid=-1;
			segments[i].pageNum=-1;
		}
		for(i=0;i<SWAP_END;i++)
		{
			swap[i].tid=-1;
			swap[i].pageNum=-1;
		}
		if((swapfd=open("mem_manager.swap",O_CREAT|O_RDWR|O_TRUNC,S_IRUSR|S_IRUSR))<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		if(lseek(swapfd,SWAP_SIZE+1,SEEK_SET)<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		write(swapfd,"",1);
		if(lseek(swapfd,0,SEEK_SET)<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		meminit=1;
	}
	if(shinit==0)
	{
		//make a first header (makes things better later)
		memHeader new;
		new.verify=VER;
		new.prev=NULL;
		new.next=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+size;
		new.free=0;
		if(size>=4*sysconf(_SC_PAGE_SIZE)-sizeof(memHeader))
		{
			memHeader rest;
			rest.free=1;
			rest.prev=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE);
			rest.next=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE);
			new.next=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE)+size;
			memcpy(mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
		}
		memcpy(mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
		shinit=1;
		return mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
	}
	char* ptr=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE);
	memHeader* bestPtr=NULL;
	int best=sysconf(_SC_PAGE_SIZE)*4;
	while(ptr!=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE))
	{
		int sz;
		if(((memHeader*)ptr)->free!=0)
		{
			sz=(((memHeader*)ptr)->next)-ptr;	
			if((abs(best-(signed)size)>abs(sz-(signed)size))&&(sz-(signed)size>=0))
			{
				best=sz;
				bestPtr=(memHeader*)ptr;
			}
		}
		ptr=((memHeader*)ptr)->next;
	}
	if(bestPtr!=NULL)
	{
		bestPtr->free=0;
		if((best-(signed)size)>sizeof(memHeader))
		{
			memHeader rest;
			rest.free=1;
			rest.prev=(char*)bestPtr;
			rest.next=bestPtr->next;
			rest.verify=VER;
			bestPtr->next=((char*)bestPtr)+size;
			memcpy(((char*)bestPtr)+size,&rest,sizeof(memHeader));
		}
		return (char*)bestPtr+sizeof(memHeader);
	}
	return NULL;
}

char* myallocate(size_t size,char* file,int line,int type)
{
	
	size+=sizeof(memHeader);
	if(size>MEM_PROT)
	{
		//cannot address that much
		return NULL;
	}
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
			perror("ERROR: ");
			exit(EXIT_FAILURE);
		}
		int i;
		for(i=0;i<SHALLOC_END;i++)
		{
			segments[i].tid=-1;
			segments[i].pageNum=-1;
		}
		for(i=0;i<SWAP_END;i++)
		{
			swap[i].tid=-1;
			swap[i].pageNum=-1;
		}
		if((swapfd=open("mem_manager.swap",O_CREAT|O_RDWR|O_TRUNC,S_IRUSR|S_IWUSR))<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		if(lseek(swapfd,SWAP_SIZE,SEEK_SET)<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		write(swapfd,"",SEEK_SET);
		if(lseek(swapfd,0,SEEK_SET)<0)
		{
			perror("ERROR: ");
			exit(1);
		}
		meminit=1;
	}
	if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)//want sig handler to put things in place for us
	{
		perror("ERROR: ");
		exit(EXIT_FAILURE);
	}
	if(type!=0)
	{
		double num=((double)(size+sizeof(memHeader)))/sysconf(_SC_PAGE_SIZE);//extra memHeader for end of mem chunk
		if(num-(int)num!=0)
		{
			num++;
		}
		int pgReq=(int)num;
		int i,has=0;
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			if(segments[i].tid==id&&segments[i].used!=0) //change to curr->tid
			{
				has++;
			}
		}
		if(has==0)
		{
			//find a free page
			int pgCount=0;
			int pgList[pgReq];
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				if(segments[i].used==0)//find enough pages to fit request
				{
					//no need to move anything as long as we own the spot
					pgList[pgCount]=i;
					pgCount++;
					if(pgCount==pgReq)
					{
						break;
					}
				}
			}
			if(pgCount!=pgReq)
			{
				int swapCount=0;
				int swapList[pgReq-pgCount];
				for(i=0;i<SWAP_SIZE;i++)
				{
					if(swap[i].used==0)
					{
						swapList[swapCount]=i;
						swapCount++;
						if(swapCount==pgReq-pgCount)
						{
							break;
						}
					}
				}
				if(swapCount+pgCount!=pgReq)
				{
					//not enough space
					return NULL;
				}
				for(i=0;i<pgCount;i++)
				{
					segments[pgList[i]].tid=id;
					segments[pgList[i]].pageNum=i;
					segments[pgList[i]].used=1;
				}
				for(i=0;i<swapCount;i++)
				{
					swap[swapList[i]].tid=id;
					swap[swapList[i]].pageNum=pgCount+i;
					swap[swapList[i]].used=1;
				}
				memHeader new,rest;
				new.verify=VER;
				new.free=0;
				new.prev=NULL;
				new.next=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+size;
				rest.verify=VER;
				rest.free=1;
				rest.prev=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE);
				rest.next=mem+(BOOK_STRT+pgReq)*sysconf(_SC_PAGE_SIZE);
				memcpy(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
				memcpy(mem+(BOOK_STRT+pgReq)*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
				return mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);				
			}
			for(i=0;i<pgReq;i++)
			{
				segments[pgList[i]].tid=id; //change to curr->id
				segments[pgList[i]].pageNum=i;
				segments[pgList[i]].used=1;
			}
			memHeader new,rest;//will have a rest section since calced for one
			new.verify=VER;
			new.free=0;
			new.prev=NULL;
			new.next=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+size;
			rest.verify=VER;
			rest.free=1;
			rest.prev=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE);
			rest.next=mem+(BOOK_STRT+pgReq)*sysconf(_SC_PAGE_SIZE);
			//memcpy will trigger SIGSEGV, but handler will find the page and swap it in
			memcpy(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
			memcpy(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
			return mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
		}
		else //we already have pages
		{
			//printf("--0\n");
			//see if request fits in already owned region
			char* ptr=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE);
			char* lastPtr=NULL;
			memHeader* bestPtr=NULL;
			int best=INT_MAX;
			while(ptr!=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE))//while we still own the pages
			{
				int sz;
				if(((memHeader*)ptr)->free!=0)
				{
					sz=(((memHeader*)ptr)->next)-ptr;
					if(abs(best-(signed)size)>abs(sz-(signed)size)&&(sz-(signed)size)>=0)
					{
						best=sz;
						bestPtr=(memHeader*)ptr;
					}
				}
				lastPtr=ptr;
				ptr=((memHeader*)ptr)->next;
			}
			if(bestPtr!=NULL)
			{
				bestPtr->free=0;
				if(best-(signed)size>sizeof(memHeader))
				{
					//can potentially create 2 contiguous free segments. Will be fixed after the closest
					//non-free segment is freed
					//could check here (will do later since not too important)
					memHeader rest;
					rest.free=1;
					rest.prev=(char*)bestPtr;
					rest.next=bestPtr->next;
					rest.verify=VER;
					bestPtr->next=((char*)bestPtr)+size;
					memcpy(((char*)bestPtr)+size,&rest,sizeof(memHeader));
				}
				return ((char*)bestPtr)+sizeof(memHeader);
			}
			//need to stick request at end
			if(((memHeader*)lastPtr)->free!=0)
			{
				//printf("--1\n");
				//stick request on end
				int roomLeft=(((memHeader*)lastPtr)->next)-lastPtr-sizeof(memHeader); //!
				double newNum=(((signed)size)-(double)roomLeft)/sysconf(_SC_PAGE_SIZE); //!
				//printf("nn=%f\n",newNum);
				if(newNum-(int)newNum!=0)
				{
					newNum++;
				}
				int newReq=(int)newNum;
				int newCount=0;
				int newList[newReq];
				//look for clean pages
				for(i=BOOK_STRT;i<BOOK_END;i++)
				{
					if(segments[i].used==0)
					{
						newList[newCount]=i;
						newCount++;
						if(newCount==newReq)
						{
							break;
						}
					}
				}
				if(newCount!=newReq)
				{
					//can't find enough mem
					int swapCount=0;
					int swapList[newReq-newCount];
					for(i=0;i<SWAP_SIZE;i++)
					{	
						if(swap[i].used==0)
						{
							swapList[swapCount]=i;
							swapCount++;
							if(swapCount==newReq-newCount)
							{
								break;
							}
						}
					}
					if(swapCount+newCount!=newReq)
					{
						return NULL;
					}
					for(i=0;i<newCount;i++)
					{
						segments[newList[i]].tid=id;
						segments[newList[i]].pageNum=has+i;
						segments[newList[i]].used=1;
					}
					for(i=0;i<swapCount;i++)
					{
						swap[swapList[i]].tid=id;
						swap[swapList[i]].pageNum=has+newCount+i;
						swap[swapList[i]].used=1;
					}
					((memHeader*)lastPtr)->free=0;
					((memHeader*)lastPtr)->next=lastPtr+size;
					memHeader rest;
					rest.free=1;
					rest.prev=lastPtr;
					rest.next=mem+(BOOK_STRT+has+newReq)*sysconf(_SC_PAGE_SIZE);
					rest.verify=VER;
					memcpy(lastPtr+size,&rest,sizeof(memHeader));
					return lastPtr+sizeof(memHeader);
				}
				for(i=0;i<newReq;i++)
				{
					segments[newList[i]].tid=id;
					segments[newList[i]].pageNum=has+i;
					segments[newList[i]].used=1;
				}
				//prev stays the same
				//verify stays the same
				((memHeader*)lastPtr)->free=0;
				((memHeader*)lastPtr)->next=lastPtr+size;
				memHeader rest;
				rest.free=1;
				rest.prev=lastPtr;
				rest.next=mem+(BOOK_STRT+has+newReq)*sysconf(_SC_PAGE_SIZE); //!
				rest.verify=VER;
				memcpy(lastPtr+size,&rest,sizeof(memHeader));
				return lastPtr+sizeof(memHeader);
			}
			else
			{
				//printf("--2\n");
				//stick request on end, but not including last chunk
				//last pointer was not free and ended right on page boundary
				//all last ptrs will have their next point to page boundary
				int pgList[pgReq];
				int pgCount=0;
				for(i=BOOK_STRT;i<BOOK_END;i++)
				{
					if(segments[i].used==0)
					{
						pgList[pgCount]=i;
						pgCount++;
						if(pgCount==pgReq)
						{
							break;
						}
					}
				}
				if(pgCount!=pgReq)
				{
					//could not find enough free pages
					int swapCount;
					int swapList[pgReq-pgCount];
					for(i=0;i<SWAP_SIZE;i++)
					{
						if(swap[i].used==0)
						{
							swapList[swapCount]=1;
							swapCount++;
							if(swapCount==pgReq-pgCount)
							{
								break;
							}
						}
					}
					if(swapCount+pgCount!=pgReq)
					{
						return NULL;
					}
					for(i=0;i<pgCount;i++)
					{
						segments[pgList[i]].tid=id;
						segments[pgList[i]].pageNum=has+i;
						segments[pgList[i]].used=1;
					}
					for(i=0;i<swapCount;i++)
					{
						swap[swapList[i]].tid=id;
						swap[swapList[i]].pageNum=has+pgCount+i;
						swap[swapList[i]].used=1;
					}
					memHeader new, rest;
					new.verify=VER;
					new.prev=lastPtr;
					new.next=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+size;
					new.free=0;
					rest.free=1;
					rest.verify=VER;
					rest.prev=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE);
					rest.next=mem+(BOOK_STRT+has+pgReq)*sysconf(_SC_PAGE_SIZE);
					memcpy(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
					memcpy(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
					return mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
				}
				for(i=0;i<pgCount;i++)
				{
					segments[i].tid=id; //chnge to curr->id
					segments[i].pageNum=has+i;
					segments[i].used=1;
				}
				memHeader new,rest;
				new.free=0;
				new.verify=VER;
				new.prev=lastPtr;
				new.next=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+size;
				rest.free=1;
				rest.verify=VER;
				rest.prev=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE);
				rest.next=mem+(BOOK_STRT+has+pgReq)*sysconf(_SC_PAGE_SIZE);
				memcpy(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
				memcpy(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
				return mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader);
			}
		}
	}
	else //sys req for mem
	{
		//just like shalloc
		if(sysinit==0)
		{
			memHeader new,rest;
			new.verify=VER;
			new.prev=NULL;
			new.next=mem+size;
			new.free=0;
			//don't need to check for room for rest
			//sys will never request for all the space in one call
			rest.verify=VER;
			rest.prev=mem;
			rest.next=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE);//until end of sys memory
			rest.free=1;
			memcpy(mem,&new,sizeof(memHeader));
			memcpy(mem+size,&rest,sizeof(memHeader));
			sysinit=1;
			return mem+sizeof(memHeader);
		}
		char* ptr=mem;
		memHeader* bestPtr=NULL;
		int best=INT_MAX;
		while(ptr!=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE))
		{
			int sz;
			if(((memHeader*)ptr)->free!=0)
			{
				sz=(((memHeader*)ptr)->next)-ptr;
				if((abs(best-(signed)size)>abs(sz-(signed)size))&&(sz-(signed)size)>=0)
				{
					best=sz;
					bestPtr=(memHeader*)ptr;
				}
			}
			ptr=((memHeader*)ptr)->next;
		}
		if(bestPtr!=NULL)
		{
			bestPtr->free=0;
			if((best-(signed)size)>sizeof(memHeader))
			{
				memHeader rest;
				rest.free=1;
				rest.verify=VER;
				rest.prev=(char*)bestPtr;
				rest.next=bestPtr->next;
				bestPtr->next=((char*)bestPtr)+size;
				memcpy(bestPtr->next,&rest,sizeof(memHeader));
			}
			return ((char*)bestPtr)+sizeof(memHeader);
		}
		return NULL; //big problem. Shouldn't happen though
	}
}

void coalesce(char* ptr,int type,int has)
{
	printf("-coal(%p)\n",ptr);
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	if(type==2)
	{
		//shalloc
		if(prv==NULL&&(char*)nxt!=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE))//on left edge
		{
			printf("-b\n");
			if(nxt->free!=0)
			{
				((memHeader*)ptr)->next=nxt->next;
				coalesce(ptr,type,has);
			}
			return;
		}
		else if((char*)nxt==mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE)&&prv!=NULL)//on right edge
		{
			printf("-c\n");
			if(prv->free!=0)
			{
				prv->next=((memHeader*)ptr)->next;
				ptr=(char*)prv;
				coalesce(ptr,type,has);
			}
			return; 
		}
		else if(prv!=NULL&&(char*)nxt!=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE))//in middle
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
				coalesce(ptr,type,has);
			}
			return;
		}

	}
	else if(type==0)
	{
		//sys
		if(prv==NULL&&(char*)nxt!=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE))//on left edge
		{
			if(nxt->free!=0)
			{
				((memHeader*)ptr)->next=nxt->next;
				coalesce(ptr,type,has);
			}
			return;
		}
		else if((char*)nxt==mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)&&prv!=NULL)//on right edge
		{
			if(prv->free!=0)
			{
				prv->next=((memHeader*)ptr)->next;
				ptr=(char*)prv;
				coalesce(ptr,type,has);
			}
			return; 
		}
		else if(prv!=NULL&&(char*)nxt!=mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE))//in middle
		{
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
				coalesce(ptr,type,has);
			}
			return;
		}
	}
	else
	{
		//usr
		char* pgEnd=mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE);
		printf("pgEnd: %p,has=%d\n",pgEnd,has);
		if(ptr==pgEnd)
		{
			return;
		}
		if(prv==NULL&&(char*)nxt!=pgEnd)//on left edge
		{
			printf("-b and %p!=%p\n",nxt,pgEnd);
			if(nxt->free!=0)
			{
				((memHeader*)ptr)->next=nxt->next;
				coalesce(ptr,type,has);
			}
			return;
			
		}
		else if(prv!=NULL&&(char*)nxt==pgEnd)//on right edge
		{
			printf("-c\n");
			if(prv->free!=0)
			{
				prv->next=((memHeader*)ptr)->next;
				ptr=(char*)prv;
				coalesce(ptr,type,has);
			}
			return; 
		}
		else if(prv!=NULL&&(char*)nxt!=pgEnd)//in middle
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
				coalesce(ptr,type,has);
			}
			return;
		}
	}
}

void mydeallocate(char* ptr,char* file,int line,int type)
{
	printf("-dealoc\n");
	memHeader* real=((memHeader*)(ptr-sizeof(memHeader)));
	//printf("ver=%d\n",real->verify);
	char* check=(char*)real;
	if((signed)(check-(mem+(SHALLOC_STRT)*sysconf(_SC_PAGE_SIZE)))>=0)
	{
		//freeing from shalloc region
		type++;
	}
	int has=0;
	if(type==1)
	{
		int i;
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			if(segments[i].tid==id&&segments[i].used!=0)//change to curr->id
			{
				has++;
			}
		}

	}
	printf("in dealloc %p - %p\n", real, real->next);
	printf("next next = %p\n",((memHeader*)(real->next))->next);
	//printf(">%lu\n",((char*)real-mem)/sysconf(_SC_PAGE_SIZE));
	if(real->verify!=VER)
	{
		printf("ERROR: Not pointing to void addr\n");
		return;
	}
	if(segments[((char*)real-mem)/sysconf(_SC_PAGE_SIZE)].tid!=id)//will be changed to look at memBook
	{
		printf("ERROR: You do not own this memory\n");
		return;
	}
	real->free=1;
	coalesce(ptr-sizeof(memHeader),type,has);
	if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)
	{
		printf("ERROR: Memory protection failure\n");
		exit(1);
	}
	return;
}

int main()
{
	char* one = (char*)malloc(3145728);
	one[5] = '1';
	printf("This is should be a 1 and it is a %c\n", *(one+5));
	char* two = (char*)malloc(3145728);
	two[5] = '1';
	printf("This is should be a 1 and it is a %c\n", *(two+5));
	char* three = (char*)malloc(3145728);
	three[5] = '1';
	printf("This is should be a 1 and it is a %c\n", *(three+5));
	char* four = (char*)malloc(3145728);
	four[5] = '1';
	printf("This is should be a 1 and it is a %c\n", *(four+5));

	
	one[5] = '2';
	four[5] = '2';
	three[5] = '2';
	two[5] = '2';

	printf("This is should be a 2 and it is a %c\n", *(one+5));
	printf("This is should be a 2 and it is a %c\n", *(two+5));
	printf("This is should be a 2 and it is a %c\n", *(three+5));
	printf("This is should be a 2 and it is a %c\n", *(four+5));

	tester* t=(tester*)malloc(sizeof(tester));
	printf("mem: %p...|%p-%p\n",mem,mem+MEM_STRT,mem+MEM_SIZE);
	//printf("SHALLOC REGION: %p - %p\n",mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE),mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE));
	printf("t Given %p\n",t);
	t->a=5;
	//////////////////
	id=2;
	tester* u=(tester*)malloc(sizeof(tester));
	printf("u Given %p\n",u);
	u->a=-2;
	/////////////////
	id=1;
	mprotect(mem,MEM_SIZE,PROT_NONE);
	printf("t again, t->a=%d\n",t->a);
	char* t2=malloc(8192);
	printf("t2 Given %p\n",t2);
	tester* t3=(tester*)malloc(sizeof(tester));
	printf("t3 Given %p\n",t3);
	int i;
	for(i=0;i<8192;i++)
	{
		t2[i]='q';
	}
	t2[8191]='\0';
//	printf("t2: %s\nt->a=%d\n",t2,t->a);
	////////
	/*
	id=2;
	mprotect(mem,MEM_SIZE,PROT_NONE);
	printf("u->a=%d\n",u->a);
	free((char*)u);
	u=(tester*)malloc(sizeof(tester));
	printf("u Given %p\n",u);
	/////// */
	id=1;
	mprotect(mem,MEM_SIZE,PROT_NONE);
	free((char*)t2);
	free((char*)t);
	t=(tester*)malloc(sizeof(tester));
	printf("t Given %p\n",t);
	printf("*** RAN TO COMPLETION ***\n");

/*
	char* t=myallocate(5000,__FILE__,__LINE__,6);
	printf("SE:%p\n",mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE));
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
	//mydeallocate(t,__FILE__,__LINE__,6);
	char* hey=myallocate(sizeof(char),__FILE__,__LINE__,6);
	printf("hey given ptr=%p\n",hey);
*/
	return 0;
}
