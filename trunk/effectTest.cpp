/***************************************************************************
 *  Copyright 2013 Penn State University
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Cruiser: concurrent heap buffer overflow monitoring using lock-free data 
 *  structures, PLDI 2011, Pages 367-377.
 *  Authors: Qiang Zeng, Dinghao Wu, Peng Liu. 
 *
 * File name: effectTest.cpp
 * Description: it contains some heap errors, like overflows, duplicate-frees.
 * 	 You can use it to test the effectiveness of Cruiser.
 * Usage: ./effectTest.out [0|1|2|3|4] (pls refer to print_usage())
 ***************************************************************************/

#include <stdio.h> 
#include <stdlib.h> // atoi
#include <unistd.h> // sleep
#include <dlfcn.h> // dlsym
#include <fcntl.h>
#include <sched.h> // sched_get_priority_max
#include <unistd.h> // getpid

//#define COUNT 100

typedef void*			(*malloc_type)(size_t);
typedef void			(*free_type)(void*);
typedef void*			(*realloc_type)(void*, size_t);
typedef void*			(*calloc_type)(size_t, size_t);
static malloc_type		original_malloc = NULL;
static free_type		original_free	= NULL;
static realloc_type		original_realloc= NULL;
static calloc_type		original_calloc = NULL;

void print_usage(){
	printf("usage: effectTest [0|1|2|3|4]:\n"
		"\t0: execute allocation family, then read/write in buffers\n"	
		"\t1: left canary corrupted then sleep\n"
		"\t2: right canary corrupted then sleep\n"
		"\t3: duplicate free\n"
		"\t4: free on an invalid address\n"
		"\t5: special cases, such as free(null), calloc(0, 6)\n"
		"\t9: other(original allocation function addresses)\n");
		
	exit(-1);	
}

void functional(int COUNT){
	void* p = malloc(0);
	//for(unsigned int i = 0; i < 1000000000000UL; i++)
		;
	free(p);
	printf("0 size buffer at malloced %p, then freed it\n", p);
	p = calloc(10, 7);
	printf("10 buffers, each 7 bytes, are calloced at %p, p[2] = %d\n", 
		p, *((int*)p + 2));
	p = realloc(p, 20);
	printf("realloc shrinked at %p and p[2]= %d\n", p, *((int*)p + 2));
	p = realloc(p, 1000);
	printf("realloc grew at %p and p[2]=%d and then will soon free\n", 
		p, *((int*)p + 2));
	free(p);
	int * pi = (int*)calloc(20, 4);
	//sleep(2);
	for(int i= 0; i < 5; i++){
		printf("%d ", pi[i]++);
	}
	for(int i= 0; i < 5; i++)
		printf("%d ", pi[i]);
	printf("\n");
	free(pi);
	printf("free on null\n");
	free(NULL);
	printf("0 object calloc %p\n", calloc(0, 100));
	printf("0 size object calloc %p\n", calloc(100, 0));
	void* pv[COUNT];
	printf("malloc/calloc/realloc massive\n");
	for(int i = 0; i<COUNT; i++){
		pv[i] = malloc(i);
		printf("malloced at pv[%d]=%p\n", i, pv[i]);
	}
	for(int i = 0; i<COUNT; i++){
		free(pv[i]);
		printf("freed at pv[%d]=%p\n", i, pv[i]);
	}
	for(int i=0; i<COUNT; i++){
		pv[i] = calloc(i, 80);	
		printf("calloced at pv[%d]=%p, objNo %d, size %d\n", i, pv[i], i, 80);
	}
	for(int i=0; i<COUNT; i++){
		pv[i] = realloc(pv[i], i*100);	
		printf("realloced at pv[%d]=%p, newSize %d\n", i, pv[i], i*100);
	}
	for(int i=0; i<COUNT; i++){
		pv[i] = realloc(pv[i], i*20);
		printf("realloced at pv[%d]=%p, newSize %d\n", i, pv[i], i*20);
	}
	for(int i = 0; i<COUNT; i++){
		free(pv[i]);	
		printf("freed at pv[%d]=%p\n", i, pv[i]);
	}
}

void left(){
	printf("left canary corrupt\n");
	int* p = (int*)malloc(100);
	p[-1] = 20;
	sleep(10);
}

void right(){
	printf("right canary corrupt\n");
	int* p = (int*)calloc(100, sizeof(int));
	p[100] = 20;
	sleep(3);
}

void duplicate(){
	void * p = calloc(10, 10);
	free(p);
	free(p);
}

void invalid(){
	int i;
	free(&i);
}

void special(){
	int i = 0;
	void *p = malloc(0);
	printf("(%d) malloc(0) = %p\n", ++i, p);
	free(p);
	printf("(%d) p = %p has been released\n", ++i, p);
	p = calloc(0, 0);
	printf("(%d) calloc(0, 0) = %p\n", ++i, p);
	free(p);
	printf("(%d) p = %p has been released\n", ++i, p);
	p = calloc(0, 7);
	printf("(c) calloc(0, 7) = %p\n", p);
	free(p);
	printf("(%d) p = %p has been released\n", ++i, p);
	p = calloc(7, 0);
	printf("(c) calloc(7, 0) = %p\n", p);
	free(p);
	printf("(%d) p = %p has been released\n", ++i, p);
	free(NULL);
	printf("(%d) free(NULL) has been called\n", ++i);

}

void other(){
	original_calloc = (calloc_type)dlsym(RTLD_NEXT, "calloc");
	if(!original_calloc){
		fprintf(stderr, "original calloc can not be resolved\n");
		exit(-1);
	}
	original_malloc = (malloc_type)dlsym(RTLD_NEXT, "malloc");
	if(!original_malloc){
		fprintf(stderr, "original malloc can not be resolved\n");
		exit(-1);
	}
	original_free = (free_type)dlsym(RTLD_NEXT, "free");
	if(!original_free){
		fprintf(stderr, "original free can not be resolved\n");
		exit(-1);
	}
	original_realloc = (realloc_type)dlsym(RTLD_NEXT, "realloc");
	if(!original_realloc){
		fprintf(stderr, "original realloc can not be resolved\n");
		exit(-1);
	}

	fprintf(stderr, "other %p, malloc %p, calloc %p, realloc %p, free %p\n", 
			other, malloc, calloc, realloc, free);
	fprintf(stderr, "other %p, Retrieved malloc %p, calloc %p, realloc %p, \
			free %p\n", other, original_malloc, original_calloc, 
			original_realloc, original_free);

	fprintf(stderr, "SCHED_OTHER: %d\n",
			sched_getscheduler(getpid()) == SCHED_OTHER? 1 : 0);
	fprintf(stderr, "Min %d, Max %d\n", sched_get_priority_min(SCHED_OTHER), 
			sched_get_priority_max(SCHED_OTHER));
	

}

int main(int argc, char ** argv){
	int option = -1;
	if( argc <= 1 || ( (option = atoi(argv[1])) < 0 ) ){
		print_usage();
	}
	int count = 0;
	if(argc >= 3)
		count = atoi(argv[2]);
	printf("Welcome to effectTest, option %d, count %d\n", option, count);
	switch(option){
		case 0:
			functional(count);
			break;
		case 1:
			left();
			break;
		case 2:
			right();
			break;
		case 3:
			duplicate();
			break;
		case 4:	
			invalid();
			break;
		case 5:
			special();
			break;
		case 9:
			other();
			break;
	}
	return 0;
}
