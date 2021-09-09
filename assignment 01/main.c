#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <signal.h>
#include <unistd.h>
#include "testcases.h"

void *buff;
unsigned long nr_signals = 0;

#define PAGE_SIZE		(4096)

#define __BALLOON_SYS_CALL_ID 442

#define SIGBALLOON 40

void sigballoon_handler(int);

long register_ballooning(void)
{
    signal(SIGBALLOON,sigballoon_handler);
    return syscall(__BALLOON_SYS_CALL_ID);
}

/*
 * 			placeholder-3
 * implement your page replacement policy here
 */

// Signal Handler
void sigballoon_handler(int sigId){
	nr_signals++;
}

int main(int argc, char *argv[])
{
	int *ptr, nr_pages;

    	ptr = mmap(NULL, TOTAL_MEMORY_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);

	if (ptr == MAP_FAILED) {
		printf("mmap failed\n");
       		exit(1);
	}
	buff = ptr;
	memset(buff, 0, TOTAL_MEMORY_SIZE);
	
	// Register with kernel ballooning subsystem
	register_ballooning();

	/* test-case */
	test_case_main(buff, TOTAL_MEMORY_SIZE);

	munmap(ptr, TOTAL_MEMORY_SIZE);
	printf("I received SIGBALLOON %lu times\n", nr_signals);
}