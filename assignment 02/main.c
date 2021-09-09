#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/user.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include "testcases.h"

void *buff;
unsigned long nr_signals = 0;

#define PAGE_SIZE		(4096)

#define __BALLOON_SYS_CALL_ID 442
#define __BALLOON_SEND_PAGES_SYS_CALL_ID 443
#define __BALLOON_DEREGISTER_SYS_CALL_ID 444

#define SIGBALLOON 40

#define PAGE_FRAME_MASK_VAL		(~(0x1ffLLU << 55))

/*Send upto MAX_PAGES_TO_SEND pages to kernel once after signal is recieved */
#define MAX_PAGES_TO_SEND 100000

int idlePageCount = 0;
int bufferPageCount = 0;

char *idleMapFilePath = "/sys/kernel/mm/page_idle/bitmap";

struct idlePage{
	unsigned long long pfn;
	struct idlePage *next;
};

struct idlePage *my_idle_pages=NULL;

struct idlePage *lastPointer;

void sigballoon_handler(int);

/* Ballooning System calls*/
long register_ballooning(void)
{
	signal(SIGBALLOON,sigballoon_handler);
	return syscall(__BALLOON_SYS_CALL_ID);
}

long send_pages_to_kernel(void)
{
	return syscall(__BALLOON_SEND_PAGES_SYS_CALL_ID,my_idle_pages);
}

long deregister_ballooning(void)
{
	return syscall(__BALLOON_DEREGISTER_SYS_CALL_ID);
}

void delete_idle_pages_list(){
	struct idlePage *current, *next;
	current = my_idle_pages;
	while(current !=NULL){
		next = current->next;
		munmap(current, sizeof(struct idlePage));
		current = next;
	}
	my_idle_pages=NULL;
}

void append_pfn_to_idle_pages(unsigned long long _pfn){


	struct idlePage *ptr = mmap(NULL, sizeof(struct idlePage), PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	ptr->pfn = _pfn;
	ptr->next = NULL;
	if(my_idle_pages==NULL){
		my_idle_pages = ptr;
		lastPointer = ptr;
	}
	else{
		lastPointer->next = ptr;
		lastPointer = ptr;
	}
}

/* Page Replacement algorithm and Locality check*/
int check_locality(unsigned long long address, int PAGEMAP_FD, int IDLEMAP_FD){

	/* Loop variable */
	unsigned long long p;
	unsigned long long pageMapSeekBits;
	unsigned long long pageFrameNo;
	unsigned long long idleMapSeekBits;
	unsigned long long frameIdleBits;

	if(address-3*PAGE_SIZE <buff){
		return 0;
	}


	for(p=address-3*PAGE_SIZE;p<address+3*PAGE_SIZE;p=p+PAGE_SIZE)
	{
		pageMapSeekBits = p * 8 / PAGE_SIZE;

		if (lseek(PAGEMAP_FD, pageMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

		if (read(PAGEMAP_FD, &pageFrameNo, sizeof (pageFrameNo)) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}
		pageFrameNo = pageFrameNo & PAGE_FRAME_MASK_VAL;

		idleMapSeekBits = (pageFrameNo / 64) * 8;
		if (lseek(IDLEMAP_FD, idleMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

		/* Read the Idle bits of the current page frame number */
		if (read(IDLEMAP_FD, &frameIdleBits, sizeof (frameIdleBits)) <= 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}
		if (!(frameIdleBits & (1ULL << (pageFrameNo % 64)))) {
			return 0;
		}
	}

	return 1;

}


int collect_idle_pages(){

	char pageMapPath[200];
	int PAGEMAP_FD, IDLEMAP_FD;
	pid_t pid = getpid();

	/* Loop variable */
	unsigned long long p;
	unsigned long long pageMapSeekBits;
	unsigned long long pageFrameNo;
	unsigned long long idleMapSeekBits;
	unsigned long long frameIdleBits;

	/* Reset the page counts */
	idlePageCount = 0;
	bufferPageCount = 0;

	/* Set Page map Path for current process */
	if (sprintf(pageMapPath, "/proc/%d/pagemap", pid) < 0) {
		printf("Unable to set the page map path");
		return 0;
	}

	/* Open the pagemap file for the current process*/
	if ((PAGEMAP_FD = open(pageMapPath, O_RDONLY)) < 0) {
		printf("Unable to open the pagemap file for process %d",pid);
		close(IDLEMAP_FD);
		return 0;
	}

	/* Open the idlemap file */
	if ((IDLEMAP_FD = open(idleMapFilePath, O_RDONLY)) < 0) {
		printf("Unable to open the idle map file for process %d",pid);
		close(IDLEMAP_FD);
		close(PAGEMAP_FD);
		return 0;
	}

	/* Remove the old idle pages list before creating new*/
	delete_idle_pages_list();

	for(p=buff;p<=(buff + TOTAL_MEMORY_SIZE);p+=PAGE_SIZE){
		pageMapSeekBits = p * 8 / PAGE_SIZE;
		if (lseek(PAGEMAP_FD, pageMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

		if (read(PAGEMAP_FD, &pageFrameNo, sizeof (pageFrameNo)) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}
		pageFrameNo = pageFrameNo & PAGE_FRAME_MASK_VAL;
		if (pageFrameNo == 0)
			continue;

		bufferPageCount++;

		/* Locate the Idle bits of the current page frame number */
		idleMapSeekBits = (pageFrameNo / 64) * 8;
		if (lseek(IDLEMAP_FD, idleMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

		/* Read the Idle bits of the current page frame number */
		if (read(IDLEMAP_FD, &frameIdleBits, sizeof (frameIdleBits)) <= 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}
		if (frameIdleBits & (1ULL << (pageFrameNo % 64))) {

			int eligible = check_locality(p, PAGEMAP_FD, IDLEMAP_FD);
			//printf("eligible :  %d\n",eligible);
			if(eligible){
				append_pfn_to_idle_pages(p);
				idlePageCount++;
			}
			if(idlePageCount > MAX_PAGES_TO_SEND){
				break;
			}

		}



	}
	close(PAGEMAP_FD);
	close(IDLEMAP_FD);
	return 0;

}

int set_all_pages_idle(){

	char pageMapPath[200];

	int PAGEMAP_FD, IDLEMAP_FD;
	pid_t pid = getpid();

	/* Loop variable */
	unsigned long long p;
	unsigned long long pageMapSeekBits;
	unsigned long long pageFrameNo;
	unsigned long long idleMapSeekBits;
	unsigned long long frameIdleBits;

	/* Set Page map Path for current process */
	if (sprintf(pageMapPath, "/proc/%d/pagemap", pid) < 0) {
		printf("Unable to set the page map path");
		return 0;
	}

	/* Open the pagemap file for the current process*/
	if ((PAGEMAP_FD = open(pageMapPath, O_RDONLY)) < 0) {
		printf("Unable to open the pagemap file for process %d",pid);
		close(IDLEMAP_FD);
		return 0;
	}

	/* Open the idlemap file */
	if ((IDLEMAP_FD = open(idleMapFilePath, O_WRONLY)) < 0) {
		printf("Unable to open the idle map file for process %d",pid);
		close(IDLEMAP_FD);
		close(PAGEMAP_FD);
		return 0;
	}

	for(p=buff;p<=(buff + TOTAL_MEMORY_SIZE);p+=PAGE_SIZE){

		pageMapSeekBits = p * 8 / PAGE_SIZE;
		if (lseek(PAGEMAP_FD, pageMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

		if (read(PAGEMAP_FD, &pageFrameNo, sizeof (pageFrameNo)) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}
		pageFrameNo = pageFrameNo & PAGE_FRAME_MASK_VAL;
		if (pageFrameNo == 0)
			continue;

		idleMapSeekBits = (pageFrameNo / 64) * 8;
		if (lseek(IDLEMAP_FD, idleMapSeekBits, SEEK_SET) < 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}


		frameIdleBits = ~0ULL;
		if (write(IDLEMAP_FD, &frameIdleBits, sizeof (frameIdleBits)) <= 0) {
			close(PAGEMAP_FD);
			close(IDLEMAP_FD);
			return 0;
		}

	}
	close(PAGEMAP_FD);
	close(IDLEMAP_FD);
	return 1;

}


/* SIGBALLOON Signal Handler */
void sigballoon_handler(int sigId){
	nr_signals++;
	printf("\n ==COLLECTING IDLE PAGES FOR KERNEL==\n");
	collect_idle_pages();
	printf("Scanned Buffer Pages : %d\n",bufferPageCount);
	printf("IDLE Pages: %d\n",idlePageCount);



	struct idlePage *current;
	current = my_idle_pages;
	while(current !=NULL){
		madvise(current->pfn, PAGE_SIZE, MADV_DONTNEED);
		current = current->next;
	}
	/* Feedback to inform kernel that signal response is resolved.
	 * So that kernel can send the signal again if there 
	 * is low memory. 
	 */
	send_pages_to_kernel();
}

int main(int argc, char *argv[])
{
	int *ptr, nr_pages;
	pid_t pid = getpid();
	ptr = mmap(NULL, TOTAL_MEMORY_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

	printf("PID of Process:%d\n",pid);

	if (ptr == MAP_FAILED) {
		printf("mmap failed\n");
		exit(1);
	}
	buff = ptr;

	memset(buff, 0, TOTAL_MEMORY_SIZE);

	if(set_all_pages_idle()){
		printf("All the pages for process: %d set as idle successfully\n",pid);
	}

	collect_idle_pages();
	printf("Scanned Buffer Pages: %d\n",bufferPageCount);
	printf("IDLE Pages: %d\n",idlePageCount);


	/* Register with kernel ballooning subsystem */
	register_ballooning();


	/* test-case */
	test_case_main(buff, TOTAL_MEMORY_SIZE);


	// collect_idle_pages();
	// printf("IDLE Pages: %d\n",idlePageCount);


	munmap(ptr, TOTAL_MEMORY_SIZE);
	printf("I received SIGBALLOON %lu times\n", nr_signals);

	/* Deregister with kernel ballooning subsystem */
	deregister_ballooning();

	/* Added the exit variable just to keep the process running */
	// int exit_var;
	// scanf("%d",&exit_var);	

}
