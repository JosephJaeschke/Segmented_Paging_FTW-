// File:	my_pthread.c
// Author:	Yujie REN
// Date:	09/23/2017

// name: Joseph Jaeschke, Crystal Calandra, Adeeb Kebir
// username of iLab: jjj93, crystaca, ask171
// iLab Server: cray1

#include "my_pthread_t.h"


#define MAX_STACK 65536 //my TA told someone that 32k is a good number
#define MAX_THREAD 32 //TA said a power of 2 and referenced 32
#define MAX_MUTEX 32 //TA said a power of 2 and referenced 32
#define MAINTENANCE 10 //not sure a good value
#define PRIORITY_LEVELS 5 //not sure good value
//for mem manager
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

short mode=0; //0 for SYS, 1 for USR
short ptinit=0; //init main stuff at first call of pthread_create
short maintenanceCounter=MAINTENANCE;
my_pthread_t idCounter=0;
int activeThreads=0;
ucontext_t ctx_main, ctx_sched,ctx_clean;
tcb* curr;
struct itimerval timer;
//for mem manager
char* mem;
int meminit=0; //has memory been initialized?
int sysinit=0; //has system used memory yet?
int shinit=0; //has shalloc been called yet?
struct sigaction sa;
memBook segments[2048]={0}; //metadata about memory 
memBook swap[4096]={0}; //metadata about swap
int swapfd; //swap file descriptor
my_pthread_mutex_t mem_lock;
my_pthread_mutex_t shalloc_lock;
my_pthread_mutex_t free_lock;
my_pthread_mutex_t join_lock;
my_pthread_mutex_t create_lock;
my_pthread_mutex_t exit_lock;
my_pthread_mutex_t yield_lock;



/////////////////////////////////////////////////////////////////////////////
//									   //
//			  MEMORY MANAGER FUNCTIONS			   //
//									   //
/////////////////////////////////////////////////////////////////////////////

static void handler(int signum,siginfo_t* si,void* unused)
{
	char* addr=(char*)si->si_addr;
	//assuming sys is not protected (since always loaded and will slow sched if protected)
	if(addr>=mem&&addr<=mem+MEM_SIZE)
	{
		my_pthread_t id;
		//printf("(sh) my bad...\n");
		if(curr==NULL)
		{
			id=0;
		}
		else
		{
			id=curr->tid;
		}
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
		if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_READ|PROT_WRITE)==-1)
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
				if(lseek(swapfd,i*sysconf(_SC_PAGE_SIZE),SEEK_SET)<0)
				{
					perror("ERROR: ");
					exit(1);
				}
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
		printf("(sh) Segmentation fault (couldn't find page for thread %d)\n",id);
		exit(EXIT_FAILURE);
	}
	else
	{
		//real segfault
		printf("(sh) Segmentation Fault (this was real) @ %p\n",addr);
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

void* shalloc(size_t size)
{
	size+=sizeof(memHeader);
	if(size+sizeof(memHeader)>4*sysconf(_SC_PAGE_SIZE))
	{
		//too big for shalloc region
		return NULL;
	}
	if(meminit==0)
	{
		meminit=1;
		mem=(char*)memalign(sysconf(_SC_PAGE_SIZE),MEM_SIZE);
		//printf("mem:%p - %p\n",mem,mem+MEM_SIZE);
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
		my_pthread_mutex_init(&mem_lock,NULL);
		my_pthread_mutex_init(&shalloc_lock,NULL);
		my_pthread_mutex_init(&free_lock,NULL);
	}
	my_pthread_mutex_lock(&shalloc_lock);
	if(shinit==0)
	{
		//make a first header (makes things better later)
		shinit=1;
		memHeader new;
		new.verify=VER;
		new.prev=NULL;
		new.next=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+size;
		new.free=0;
		if(size+sizeof(memHeader)<=4*sysconf(_SC_PAGE_SIZE))
		{
			memHeader rest;
			rest.free=1;
			rest.prev=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE);
			rest.next=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE);
			new.next=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+size;
			memcpy(mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+size,&rest,sizeof(memHeader));
		}
		memcpy(mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE),&new,sizeof(memHeader));
		my_pthread_mutex_unlock(&shalloc_lock);
		return ((void*)(mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader)));
	}
	char* ptr=mem+SHALLOC_STRT*sysconf(_SC_PAGE_SIZE);
	memHeader* bestPtr=NULL;
	int best=sysconf(_SC_PAGE_SIZE)*4;
	while(ptr!=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE))
	{
		int sz;
//printf("%p!=%p\n",ptr,mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE));
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
		my_pthread_mutex_unlock(&shalloc_lock);
		return ((void*)(((char*)bestPtr)+sizeof(memHeader)));
	}
	my_pthread_mutex_unlock(&shalloc_lock);
	return NULL;
}

void* myallocate(size_t size,char* file,int line,int type)
{
	size+=sizeof(memHeader);
	if(size>MEM_PROT)
	{
		//cannot address that much
		return NULL;
	}
	if(meminit==0)
	{
		meminit=1;
		mem=(char*)memalign(sysconf(_SC_PAGE_SIZE),MEM_SIZE);
		//printf("mem:%p - %p\n",mem,mem+MEM_SIZE);
		//printf("header size:%lu,0x%X\n",sizeof(memHeader),(int)sizeof(memHeader));	
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
		my_pthread_mutex_init(&mem_lock,NULL);
		my_pthread_mutex_init(&shalloc_lock,NULL);
		my_pthread_mutex_init(&free_lock,NULL);
	}
	if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)//want sig handler to put things in place for us
	{
		perror("ERROR: ");
		exit(EXIT_FAILURE);
	}

	if(type!=0)
	{
		my_pthread_mutex_lock(&mem_lock);
		my_pthread_t id;
		if(curr==NULL)
		{
			//called malloc before a thread was made
			id=0;
		}
		else
		{
			id=curr->tid;
		}
		double num=((double)(size+sizeof(memHeader)))/sysconf(_SC_PAGE_SIZE);//extra memHeader for end of mem chunk
		if(num-(int)num!=0)
		{
			num++;
		}
		int pgReq=(int)num;
		int i,has=0;
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			if(segments[i].tid==id&&segments[i].used!=0)
			{
				has++;
			}
		}
		if(has==0)
		{
			//find a free page
			if(size+sizeof(memHeader)>MEM_PROT)
			{
				//max address reached
				my_pthread_mutex_unlock(&mem_lock);
				return NULL;
			}
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
					my_pthread_mutex_unlock(&mem_lock);
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
				my_pthread_mutex_unlock(&mem_lock);
				return ((void*)(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader)));
			}
			for(i=0;i<pgReq;i++)
			{
				segments[pgList[i]].tid=id;
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
			my_pthread_mutex_unlock(&mem_lock);
			return ((void*)(mem+BOOK_STRT*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader)));
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
				my_pthread_mutex_unlock(&mem_lock);
				return ((void*)(((char*)bestPtr)+sizeof(memHeader)));
			}
			//need to stick request at end
			if(((memHeader*)lastPtr)->free!=0)
			{
				if(lastPtr+size>mem+BOOK_END*sysconf(_SC_PAGE_SIZE))
				{
					//surpassing max address for a thread
					//thread cannot index past usr mem with this allocate
					my_pthread_mutex_unlock(&mem_lock);
					return NULL;
				}
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
						my_pthread_mutex_unlock(&mem_lock);
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
					my_pthread_mutex_unlock(&mem_lock);
					return ((void*)(lastPtr+sizeof(memHeader)));
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
				my_pthread_mutex_unlock(&mem_lock);
				return ((void*)(lastPtr+sizeof(memHeader)));
			}
			else
			{
				//printf("--2\n");
				//stick request on end, but not including last chunk
				//last pointer was not free and ended right on page boundary
				//all last ptrs will have their next point to page boundary
				if(((memHeader*)lastPtr)->next+size+sizeof(memHeader)>mem+MEM_PROT*sysconf(_SC_PAGE_SIZE))
				{
					//cannot address than far
					my_pthread_mutex_unlock(&mem_lock);
					return NULL;
				}
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
						my_pthread_mutex_unlock(&mem_lock);
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
					my_pthread_mutex_unlock(&mem_lock);
					return ((void*)(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader)));
				}
				for(i=0;i<pgCount;i++)
				{
					segments[i].tid=id;
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
				my_pthread_mutex_unlock(&mem_lock);
				return ((void*)(mem+(BOOK_STRT+has)*sysconf(_SC_PAGE_SIZE)+sizeof(memHeader)));
			}
		}
	}
	else //sys req for mem
	{
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
		//	printf("ptr given from %p - %p\n",mem,mem+size);
			return ((void*)(mem+sizeof(memHeader)));
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
				bestPtr->next=((char*)bestPtr)+(signed)size;
				memcpy(bestPtr->next,&rest,sizeof(memHeader));
			}
//			printf("ptr given from %p - %p\n",bestPtr,(char*)bestPtr+(signed)size);
			return ((void*)(((char*)bestPtr)+sizeof(memHeader)));
		}
		return NULL; //memory is full
	}
}

void coalesce(char* ptr,int type,int has)
{
//	printf("-coal(%p)\n",ptr);
	memHeader* nxt=(memHeader*)((memHeader*)ptr)->next;
	memHeader* prv=(memHeader*)((memHeader*)ptr)->prev;
	if(type==2)
	{
		//shalloc
		if(prv==NULL&&(char*)nxt!=mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE))//on left edge
		{
//			printf("-b\n");
			if(nxt->free!=0)
			{
				((memHeader*)ptr)->next=nxt->next;
				coalesce(ptr,type,has);
			}
			return;
		}
		else if((char*)nxt==mem+SHALLOC_END*sysconf(_SC_PAGE_SIZE)&&prv!=NULL)//on right edge
		{
//			printf("-c\n");
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
//			printf("-d\n");
			if(nxt->free!=0||prv->free!=0)
			{
				if(nxt->free!=0)
				{
//					printf("d1\n");
					((memHeader*)ptr)->next=nxt->next;
				}
				if(prv->free!=0)
				{
//					printf("d2\n");
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
//					printf("d1\n");
					((memHeader*)ptr)->next=nxt->next;
				}
				if(prv->free!=0)
				{
//					printf("d2\n");
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
//		printf("pgEnd: %p,has=%d\n",pgEnd,has);
		if(ptr==pgEnd)
		{
			return;
		}
		if(prv==NULL&&(char*)nxt!=pgEnd)//on left edge
		{
//			printf("-b and %p!=%p\n",nxt,pgEnd);
			if(nxt->free!=0)
			{
				((memHeader*)ptr)->next=nxt->next;
				coalesce(ptr,type,has);
			}
			return;
			
		}
		else if(prv!=NULL&&(char*)nxt==pgEnd)//on right edge
		{
//			printf("-c\n");
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
//			printf("-d\n");
			if(nxt->free!=0||prv->free!=0)
			{
				if(nxt->free!=0)
				{
//					printf("d1\n");
					((memHeader*)ptr)->next=nxt->next;
				}
				if(prv->free!=0)
				{
//					printf("d2\n");
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

void mydeallocate(void* ptr_to_mem,char* file,int line,int type)
{
	my_pthread_mutex_lock(&free_lock);
	char* ptr=(char*)ptr_to_mem;
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
		my_pthread_t id;
		if(curr==NULL)
		{
			//no threads made yet
			id=0;//set to would be id of main context id
		}
		else
		{
			id=curr->tid;
		}
		int i;
		for(i=BOOK_STRT;i<BOOK_END;i++)
		{
			if(segments[i].tid==id&&segments[i].used!=0)//change to curr->id
			{
				has++;
			}
		}

	}
//	printf("in dealloc %p - %p\n", real, real->next);
//	printf("next next = %p\n",((memHeader*)(real->next))->next);
	//printf(">%lu\n",((char*)real-mem)/sysconf(_SC_PAGE_SIZE));
	if(real->verify!=VER)
	{
		//printf("ERROR: Not pointing to void addr\n");
		my_pthread_mutex_unlock(&free_lock);
		return;
	}
	if(type==1)
	{
		my_pthread_t id;
		if(curr==NULL)
		{
			id=0;
		}
		else
		{
			id=curr->tid;
		}
		if(segments[((char*)real-mem)/sysconf(_SC_PAGE_SIZE)].tid!=id)
		{
			my_pthread_mutex_unlock(&free_lock);
		}
	}
	real->free=1;
	coalesce(ptr-sizeof(memHeader),type,has);
	if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)
	{
		perror("ERROR: ");
		exit(1);
	}
	my_pthread_mutex_unlock(&free_lock);
	return;
}

/////////////////////////////////////////////////////////////////////////////
//									   //
//			MEMORY MANAGER FUNCTIONS END			   //
//									   //
/////////////////////////////////////////////////////////////////////////////



void wrapper(int f1,int f2,int a1,int a2)
{
	mask p;
	p.halfs.hhalf=a1;
	p.halfs.lhalf=a2;
	mask s;
	s.halfs.hhalf=f1;
	s.halfs.lhalf=f2;

	void* a=(void*)p.data;
	void*(*f)(void*)=(void*(*)(void*))s.data;

//	printf("--start wrapping\n");
	curr->retVal=(*f)(a);
//	printf("--done wrapping\n");
//	fflush(stdout);
	if(curr->state!=4)
	{
		curr->retVal=(void*)myallocate(1,__FILE__,__LINE__,0);
		my_pthread_exit(curr->retVal);
	}
	return;
}

void alarm_handler(int signum)
{
	//printf("===ALARM=== (mode=%d)\n",mode);
	fflush(stdout);
	if(mode==0)
	{
		return;
	}
	//i put the stuff in sched. I hope it is not too slow, but it was better for debugging
	if(swapcontext(&curr->context, &ctx_sched)==-1)
	{
		printf("ERROR: Failed to swap to sched\n");
		fflush(stdout);
	}
	return;
}

void scheduler()
{
	//put remove thread stuff in because of mutex
	while(1)
	{
		//printf("-s\n");
		//fflush(stdout);
		mode=0;
//		printf("-sched\n");
//		printf("--tid:%u op:%d\n",curr->tid,curr->oldPriority);
//		printf("--tid2:%u\n",queue[curr->oldPriority]->tid);
		//printf("--sched | p=%u, op=%u\n",curr->priority,curr->oldPriority);
		//fflush(stdout);
		//remove curr from queue at old priority level
		if(curr->state!=4 && curr->state != 3)
		{
			//printf(queue[oldPriority] = %d, curr->tid = %d\n", queue[curr->oldPriority]->tid, curr->tid);
			if(queue[curr->oldPriority]->tid == curr->tid)
			{	
				queue[curr->oldPriority]=queue[curr->oldPriority]->nxt;
			}
			else
			{
				tcb *ptr, *prev;
				ptr = queue[curr->oldPriority];
				while(ptr->nxt != NULL)
				{
					if(ptr->tid == curr->tid)
					{
						prev->nxt = ptr->nxt;
						break;
					}
					prev = ptr;
					ptr = ptr->nxt;
				}
			}
			if(curr->priority<PRIORITY_LEVELS-1)//////////////////////////////
			{
				curr->priority++;
			}
			curr->oldPriority=curr->priority;///////////////////////////////
			//put old thread back in queue
			curr->nxt=NULL;
			if(queue[curr->priority]==NULL)//if empty
			{
				queue[curr->priority]=curr;
			}
			else
			{
				tcb* temp=queue[curr->priority];
				while(temp->nxt!=NULL)
				{
					temp=temp->nxt;
				}
				temp->nxt=curr;
			}
		}
		int i,found=0;
		tcb* ptr;
		curr->state=1;
//		maintenanceCounter--;
//		if(maintenanceCounter==0)
//		{
//			maintenanceCounter=MAINTENANCE;
//			maintenance();
//		}
		//try to find a thread that can be run
		for(i=0;i<PRIORITY_LEVELS;i++)
		{
			ptr=queue[i];
			while(ptr!=NULL)
			{
				if(ptr->state==1)
				{
					ptr->state=2;
					curr=ptr;
					found=1;
					break;
				}
				ptr=ptr->nxt;
			}
			if(found)
			{
//				printf("--run thread w/ id=%u p=%d op=%d\n",curr->tid,curr->priority,curr->oldPriority);
				fflush(stdout);
				break;
			}
		}
		//if there is no thread that can run
		if(!found)
		{
//			printf("--Found no thread ready to run\n");
			return;
		}
		//printf("-end s\n");
		if(mprotect(mem+MEM_STRT,MEM_PROT,PROT_NONE)==-1)
		{
			perror("ERROR: ");
			exit(1);
		}
		timer.it_value.tv_sec=0;
		timer.it_value.tv_usec=(curr->priority+1)*25000;
		setitimer(ITIMER_REAL,&timer,NULL);
		mode=1;
		//printf("scheduled thread %d\n", curr->tid);
		swapcontext(&ctx_sched,&curr->context);
	}
	return;
}

void maintenance()
{
	//printf("m\n");
	fflush(stdout);
	//give all threads priority 0 to prevent starvation
	int i;
	tcb* new=(tcb*)myallocate(sizeof(tcb),__FILE__,__LINE__,0);
	tcb* head=new;
	tcb* tmp;
	for(i=0;i<PRIORITY_LEVELS;i++)
	{
		if(queue[i]!=NULL)
		{
			new->nxt=queue[i];
			while(new->nxt!=NULL)
			{
				new->priority=0;
				new->oldPriority=0;
				new=new->nxt;
			}
			new->priority=0;
			new->oldPriority=0;
			queue[i]=NULL;
		}
	}
	queue[0]=head->nxt;
	new=head;
	mydeallocate(new,__FILE__,__LINE__,0);
	return;
}

void clean() //still need to setup context
{
	//set scheduler's uclink to this so cleanup can be done when the scheduler ends
	//i think just free stuff
	return;
}

/* create a new thread */
int my_pthread_create(my_pthread_t* thread, pthread_attr_t* attr, void*(*function)(void*), void * arg)
{
//	printf("--create\n");
	if(ptinit==0)
	{
		//init stuff
		//set up main thread/context
		queue=(tcb**)myallocate(PRIORITY_LEVELS*sizeof(tcb),__FILE__,__LINE__,0);
		terminating=(tcb*)myallocate(sizeof(tcb),__FILE__,__LINE__,0);
		int i=0;
		for(i;i<PRIORITY_LEVELS;i++)
		{
			queue[0]=NULL;
		}
		if(getcontext(&ctx_main)==-1)
		{
		//	printf("ERROR: Failed to get context for main\n");
			return 1;
		}
		tcb* maint=(tcb*)myallocate(sizeof(tcb),__FILE__,__LINE__,0);
		maint->state=0;
		maint->tid=idCounter++;
		maint->context=ctx_main;
		maint->retVal=NULL;
		maint->priority=0;
		maint->oldPriority=0;
		maint->nxt=NULL;
		maint->state=1;
		curr=maint;
		queue[0]=maint;
//		printf("--Got main context\n");
		//set up context for cleanup
//		if(getcontext(&ctx_clean)==-1)
//		{
//			printf("ERROR: Failed to get context for cleanup\n");
//			return 1;
//		}
//		makecontext(&ctx_clean,clean,0);

		//set up scheduler thread/context
		if(getcontext(&ctx_sched)==-1)
		{
		//	printf("ERROR: Failed to get context for scheduler\n");
			return 1;
		}
		ctx_sched.uc_link=0;
		ctx_sched.uc_stack.ss_sp=(void*)myallocate(MAX_STACK,__FILE__,__LINE__,0);
		ctx_sched.uc_stack.ss_size=MAX_STACK;
		/*
		tcb* schedt=malloc(sizeof(tcb*));
		schedt->state=0;
		activeThreads++;
		schedt->tid=idCounter++;
		schedt->context=ctx_sched;
		schedt->retVal=NULL;
		schedt->timeslice=0;
		schedt->priority=0;
		schedt->nxt=NULL;
		schedt->state=1;
		*/
		makecontext(&ctx_sched,scheduler,0);
//		printf("--Got context for scheduler\n");

		//set first timer
		signal(SIGALRM,alarm_handler);
		timer.it_value.tv_sec=0;	
		timer.it_value.tv_usec=25000; 
		setitimer(ITIMER_REAL,&timer,NULL);
//		printf("--timer set\n");
		my_pthread_mutex_init(&join_lock,NULL);
		my_pthread_mutex_init(&exit_lock,NULL);
		my_pthread_mutex_init(&create_lock,NULL);
		my_pthread_mutex_init(&yield_lock,NULL);
		ptinit=1;
	}

	
	my_pthread_mutex_lock(&create_lock);
	if(activeThreads==MAX_THREAD)
	{
		//printf("ERROR: Maximum amount of threads are made, could not make new one\n");
		my_pthread_mutex_unlock(&create_lock);
		return 1;
	}
	ucontext_t ctx_func;
	if(getcontext(&ctx_func)==-1)
	{
		//printf("ERROR: Failed to get context for new thread\n");
		my_pthread_mutex_unlock(&create_lock);
		return 1;
	}
	ctx_func.uc_link=&curr->context;
	ctx_func.uc_stack.ss_sp=(void*)myallocate(MAX_STACK,__FILE__,__LINE__,0);
	ctx_func.uc_stack.ss_size=MAX_STACK;
	tcb* t=(tcb*)myallocate(sizeof(tcb),__FILE__,__LINE__,0);
	t->state=0;
	t->tid=idCounter++;
	*thread=t->tid;
	t->context=ctx_func;
	t->retVal=NULL;
	t->priority=0;
	t->oldPriority=0;
	t->nxt=NULL;
	t->state=1;


	mask params;
	params.data=arg;
	mask subroutine;
	subroutine.data=function;

	makecontext(&t->context,(void(*)(void))wrapper,4,subroutine.halfs.hhalf,subroutine.halfs.lhalf,params.halfs.hhalf,params.halfs.lhalf);


	if(queue[0]==NULL)
	{
		queue[0]=t;
	}
	else
	{
		tcb* ptr=queue[0];
		while(ptr->nxt!=NULL)
		{
			ptr=ptr->nxt;
		}
		ptr->nxt=t;
	}
//	printf("--added to queue, id %u\n",t->tid);
	activeThreads++;
	my_pthread_mutex_unlock(&create_lock);
	return 0;
}

/* give CPU pocession to other user level threads voluntarily */
int my_pthread_yield()
{
//	printf("--yield\n");
	curr->priority = PRIORITY_LEVELS-1;
	swapcontext(&curr->context,&ctx_sched);
}

/* terminate a thread */
void my_pthread_exit(void* value_ptr)
{

	my_pthread_mutex_lock(&exit_lock);
//	printf("--exit\n");
	if(value_ptr==NULL)
	{
		printf("ERROR: value_ptr is NULL\n");
		my_pthread_mutex_unlock(&exit_lock);
		return;
	}
	//mark thread as terminating
	curr->state=4;
	//remove curr from queue
	if(queue[curr->priority]->tid==curr->tid)
	{
		queue[curr->priority]=queue[curr->priority]->nxt;
	}
	else
	{
		tcb* ptr;
		ptr=queue[curr->priority];
		tcb* prev;
		while(1)
		{
			if(ptr->tid==curr->tid)
			{
				prev->nxt=ptr->nxt;
				break;
			}
			prev=ptr;
			ptr=ptr->nxt;
		}
	}
	//add curr to terminating list
	if(terminating==NULL)
	{
		terminating=curr;
		curr->nxt=NULL;
	}
	else
	{
		curr->nxt=terminating;
		terminating=curr;
		
	}
	//set value_ptr to retVal of terminating thread?
	curr->retVal=value_ptr;
	//yield thread
	my_pthread_mutex_unlock(&exit_lock);
//	printf("Thread %d yielding from exit\n", curr->tid);
	my_pthread_yield();
	return;
}

/* wait for thread termination */
int my_pthread_join(my_pthread_t thread, void **value_ptr)
{

	my_pthread_mutex_lock(&join_lock);
//	printf("--join\n");
	//mark thread as waiting
	curr->state=5;
	//look for "thread" in terminating list
	while(1)
	{
		if(terminating==NULL)
		{
			continue;
		}
		else if(terminating->tid==thread)//thread is first in list
		{
			tcb* ptr=(tcb*)myallocate(sizeof(tcb),__FILE__,__LINE__,0);
			ptr=terminating;
			//dereference ** to set equal to return thing
			//to deref **, cast to double (sizeof(double)=sizeof(pointer))
			//goto location and set what void* retVal points to
			if(value_ptr!=NULL)
			{	
				double** temp=(double**)value_ptr;
				*temp=terminating->retVal;	
			}
			terminating=terminating->nxt;
			if(ptr->context.uc_stack.ss_sp!=NULL)
			{
				mydeallocate((ptr->context.uc_stack.ss_sp),__FILE__,__LINE__,0);
			}
			mydeallocate(ptr,__FILE__,__LINE__,0);
			activeThreads--;
			curr->state=1;
			int i;
			for(i=BOOK_STRT;i<BOOK_END;i++)
			{
				if(segments[i].tid==thread)
				{
					segments[i].used=0;
					segments[i].tid=-1;
					segments[i].pageNum=-1;
				}
			}
			for(i=0;i<SWAP_END;i++)
			{
				if(swap[i].tid==thread)
				{
					swap[i].used=0;
					swap[i].tid=-1;
					swap[i].pageNum=-1;
				}
			}
			my_pthread_mutex_unlock(&join_lock);
			return 0;
		}
		else//thread is not first in list
		{
			tcb* ptr=terminating;
			tcb* prev=ptr;
			ptr=ptr->nxt;
			while(ptr!=NULL)
			{
				if(ptr->tid==thread)
				{
					prev->nxt=ptr->nxt;
					if(value_ptr!=NULL)
					{
						double** temp=(double**)value_ptr;
						*temp=ptr->retVal;
					}
					//printf("Thread %d has been successfully waited on by Thread %d\n", (int) thread, curr->tid);
					mydeallocate(ptr->context.uc_stack.ss_sp,__FILE__,__LINE__,0);
					mydeallocate(ptr,__FILE__,__LINE__,0);
					activeThreads--;
					my_pthread_mutex_unlock(&join_lock);
					return 0;
				}
				prev = ptr;
				ptr=ptr->nxt;
			}
		}
		
//		printf("Thread %d not found in terminating list, waited on by Thread %d\n", (int) thread, curr->tid);
		my_pthread_mutex_unlock(&join_lock);
//		printf("Thread %d yielding from join\n", curr->tid);
		my_pthread_yield();
	}
	my_pthread_mutex_unlock(&join_lock);
	return 0;
}
/* initial the mutex lock */
int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr) 
{

//	printf("mutex_init\n");
	if (mutexList == NULL)
	{
		mutexList =  mutex;
	}else{
		my_pthread_mutex_t *ptr = mutexList;
		while (ptr->next != NULL)
		{
			ptr = ptr->next;
			
		}
		ptr->next = mutex;
	}
	mutex->locked = 0;
	mutex->maxP=PRIORITY_LEVELS;
	//mutex->has = curr;
	mutex->next = NULL;
	//printf("mutex initialized, locked = %d\n", mutex->locked);
	return 0;
}

/* aquire the mutex lock */
int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) 
{
//	printf(" mutex_lock\n");
	int lockStatus;
	while((lockStatus = __atomic_test_and_set(&mutex->locked, __ATOMIC_SEQ_CST)) == 1)
	{
		
		//mutex already locked, change state to waiting
		curr->state = 3;
	//	printf("mutex was already locked!!! OH NOOOS!! state = %d\n", curr->state);
		//remove curr from ready queue
		if (queue[curr->priority]->tid == curr->tid)
		{
			queue[curr->priority] = curr->nxt;
		}else{
			tcb *ptr, *prev;
			ptr = queue[curr->priority];
			while(ptr->nxt != NULL)
			{
				if (ptr->tid == curr->tid)
				{
					prev->nxt = ptr->nxt;
					break;
				}
				prev = ptr;
				ptr = ptr->nxt;
			}
		}
	//	printf("mutex removed curr from ready!!!!!!!!!!!!!\n");
		
		//place curr at the end of waiting queue for this mutex
		if(mutex->waiting == NULL)
		{
			mutex->waiting = curr;
		}else{
			tcb *ptr = mutex->waiting;
			while(ptr->nxt != NULL)
			{
				ptr = ptr->nxt;
			}
			ptr->nxt = curr;
		}
		//printf("Thread %d yielding from mutex lock\n", curr->tid);
		my_pthread_yield();
	}
	if (lockStatus == 2){
		//printf("ERROR: attempting to lock 'destroyed' mutex\n");
		return 1;
	}

	//printf("mutex was not locked. mutex is belong to me now! mwuahahahaha!\n");
	return 0;
}

/* release the mutex lock */
int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex) 
{
//	printf(" mutex_unlock\n");
	//double check that mutex is not already unlocked (just in case)
	if(mutex->locked == 0)
	{
		//printf("ERROR: Attempting to unlock UNLOCKED mutex\n");
		return 1;
	}

	//update mutex_locked status and place next in waiting queue into ready queue
	__atomic_clear(&mutex->locked, __ATOMIC_SEQ_CST);
	if (mutex->waiting != NULL)
	{
		tcb *ptr = mutex->waiting;
		mutex->waiting = ptr->nxt;
		ptr->state = 1; //change state to ready
		ptr->nxt = queue[0]; //add to front of priority queue for now
		queue[0] = ptr;	//need to properly add
		ptr->priority = 0; //reintegrating into society
		ptr->oldPriority = 0;
	}
	//printf("mutex should be unlocked now and next in waiting queue is now ready!\n");
	return 0;
}

/* destroy the mutex */
int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex) 
{
//	printf("mutex_destroy\n");
	//cannot destroy a locked mutex
	if (mutex->locked == 1)
	{
		printf("ERROR: Attempting to destroy LOCKED mutex\n");
		return 1;
	}
	//change status to "destroyed" and remove from mutexList
	mutex->locked = 2;
	return 0;
	my_pthread_mutex_t *prev, *ptr;
	printf("ml@ %p\n",mutexList);
	ptr = mutexList;
	while (ptr->next != NULL)
	{
		if (ptr == mutex)
		{
			if (prev == NULL)
			{
				printf("half way**************\n");
				mutexList = mutex->next;
				mutex->next = NULL;
			}
			else
			{
				printf("prev=%p mutex=%p\n",prev->next,mutex->next);
				prev->next = mutex->next;
			}
		}

		else{
			prev = ptr;
			ptr = ptr->next;
		}
	}
	//printf("mutex should be removed from mutexList and lock status = %d\n", mutex->locked);
	return 0;
}

