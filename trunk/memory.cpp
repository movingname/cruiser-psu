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
 ***************************************************************************/

#include <math.h>
#include<fcntl.h>
#include <sys/mman.h> // mmap, munmap
#include <string.h>
#include "monitor.h"

// It is insufficient to avoid global namespace pollution solely by declaring
// all functions static; classe and structure names are still polluting.
// So we use namespace.
namespace cruiser{

void beforeExit(void);

// Retrieve original memory allocation function addresses and create cruiser 
// threads. The constructor function is invoked automatically by libc.
void __attribute__((constructor)) init(){
	// If SELinux is involved, a malloc call would be issued before the 
	// automatic init call, so we have to initialize explicitly in our malloc:
	// "if(!g_initialized) init();" // if is redundant; it is for better perf.
	// init will be called once again as the constructor. 
	// The three lines below can prevent such duplicate initialization.
	// To achieve thread-safety, we can use Compare_And_Swap, but it is fine
	// in this case, as it is very unlikely that we have the thread safety 
	// issue in the very early stage of process creation.
	if (g_initialized)
		return;
	g_initialized = 1; 

	g_pid = getpid();
	g_init_begin_time = getUsTime();
	g_exit_procedure = RUNNING;

	// When a SPEC benchmark is started, multiple processes are created.
	// For example, some are for the measurement purpose.
	// Only "run_base_ref" is the target program we need to protect.
#ifdef SPEC 
	const char *pstr = "../run_base_ref";
	t_protect = !strncmp(program_invocation_name, pstr, strlen(pstr));
#endif

// This is commented out, because it doesn't work for APACE.
//#ifdef APACHE
//	char *pstr = "/usr/sbin/apache2";
//	t_protect = !strncmp(program_invocation_name, pstr, strlen(pstr));
//#endif //APACHE

	//fopn may allocate memory, so I use the raw open
	//int fd = open("/dev/urandom", O_RDONLY);
	//if(fd >  0){
	//	read(fd, &g_canary, sizeof g_canary);
	//	actually, read once then seed rand() is sufficient.
	//	read(fd, &g_canary_free, sizeof g_canary_free);
	//	close(fd);
	//}
	//else{
		g_canary = 0xcccccccc; //0x87654321;
#ifdef DELAYED
		g_canary_free = 0xfefefedd; //0xfedcba98;
		g_canary_realloc = 0x10101010;
#endif //DELAYED
	//}

//#ifdef CRUISER_DEBUG
	//fprintf(stderr, "Init is started by main thread %d\n", pthread_self());
//#endif

	// According to the execution trace, dlsym() calls __malloc_initialize_hook 
	// if it is not NULL. In case malloc or free is called due to the hook, we 
	// disable the hook transiently. 

	void (*saved_malloc_initialize_hook) (void) = __malloc_initialize_hook;
	__malloc_initialize_hook = NULL;

	// Obtain the function pointer of original malloc/free/realloc
	// Don't use dlopen("libc.so"), for it will call malloc, which is not ready.

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
	// Now the original malloc/free/realloc can be called safely, 
	// e.g. in "new ThreadRecordList"
	
	__malloc_initialize_hook = saved_malloc_initialize_hook;
	
#ifdef SPEC
	// SPEC assistant programs can return now.
	// However, malloc/free calls are still hooked due to the LD_PRELOAD trick,
	// which is the reason that we have to retrieve those original allocaition
	// function pointers and then pass the calls to them when they are hooked.
	if(!t_protect)
		return;
#endif //SPEC

	if(t_protect){
		t_protect = 0;
		if(!g_threadrecordlist)
			g_threadrecordlist = new ThreadRecordList;
		t_protect = 1;
	}
	
	// NMONITOR: hook the malloc/free call and encapsulate the buffer; but
	//			the monitor and transmitter threads are not created.
	// NPROTECT: hook malloc/free merely (t_protect = 0).
#ifndef NMONITOR 
	// Create the monitor thread, which will creates the transmitter thread 
	int thread_ret = pthread_create(&g_monitor, NULL, monitor, NULL);
	if(thread_ret){
		fprintf(stderr, "Error: monitor thread cannote be created (%d)\n", 
				thread_ret);
		exit(-1);
	}
#endif

	// fopen calls malloc, so
	// this has to be after the original_malloc family is retrieved;
	// otherwise, there will be segfault.
#if defined(EXP) || defined(SINGLE_EXP)
	FILE *fp = fopen("~/cruiser.log", "a");
	fp = fopen("~/cruiser.log", "a");	
	if(!fp)
		fp = stderr;
#ifdef DELAYED
	const char *str = "Lazy";
#else
	const char *str = "Eager";
#endif
	fprintf(fp, "\n\n\n%s Cruiser(pid %lu, thread ID %lu), program:%s at %u\n", 
			str, (unsigned long)g_pid, (unsigned long)pthread_self(), 
			program_invocation_name, g_init_begin_time );
	fprintf(fp, "init time %u us\n", getUsTime() - g_init_begin_time);
	if(fp != stderr)
		fclose(fp);
#endif //defined(EXP) || defined(SINGLE_EXP)

#ifdef CRUISER_DEBUG
	fprintf(stderr, "Init is finished by the main thread %lu\n", 
		(unsigned long)(pthread_self()));
	fprintf(stderr, "Retrieved malloc %p, calloc %p, realloc %p, free %p\n", 
		original_malloc, original_calloc, original_realloc, original_free);
	fprintf(stderr, "In cruiser malloc %p, calloc %p, realloc %p, free %p\n",
		malloc, calloc, realloc, free);
#endif

	if(atexit(beforeExit))
		fprintf(stderr, "Error: atexit(beforeExit) failed");

	// cruiser threads won't start real monitoring until g_initialized == 2, 
	// in order to let init() finish first. This may be unnecessary.
	g_initialized = 2;
}

// Some programs, e.g. tar, don't call beforeExit at exit. 
// I don't know the exact reason so I invoke beforeExit somewhere explicitly. 
// The weird logic is only for tar.
void beforeExit(void){
#ifdef EXP
	unsigned endTime = getUsTime();
	FILE *fp = fopen("~/cruiser.log", "a");
	if (!fp)
		fp = stderr;
	fprintf(fp, "\nBefore exit program:%s (pid %lu, thread id %lu)\n", 
		program_invocation_name, 
		(unsigned long)getpid(), 
		(unsigned long)pthread_self());
	fprintf(fp, "End at %u\nMonitor duration %u us\n", 
		endTime, endTime - g_init_begin_time);
	fprintf(fp, "Monitor round %lu, total check count %llu, avg cycle %.2f\n", 
		g_roundCount, g_totalCheckCount, 
		(endTime - g_init_begin_time) / double(g_roundCount));
	fprintf(fp, "Total: max list length %u, avg list length %.2f\n", 
		g_maxRoundBufferCount, g_avgRoundBufferCount);
#ifdef DELAYED
	fprintf(fp, "Live: max buffer count %u, max buffer size %u, avg buffer \
		count %.2f, avg buffer size %.2f\n", 
		g_maxLiveBufferCount, g_maxLiveBufferSize, 
		g_avgLiveBufferCount, g_avgLiveBufferSize);
	fprintf(fp, "Delayed: max buffer count %u, max buffer size %u, avg buffer \
		count %.2f, avg buffer size %.2f\n", 
		g_maxDelayedBufferCount, g_maxDelayedBufferSize, 
		g_avgDelayedBufferCount, g_avgDelayedBufferSize);
#else
	fprintf(fp, "Live: max buffer count %u, avg buffer count %.2f\n", 
		g_maxLiveBufferCount, g_avgLiveBufferCount);
	fprintf(fp, "Signal: max buffer count %u, avg buffer count %.2f\n", 
		g_maxSignalBufferCount, g_avgSignalBufferCount);
#endif //DELAYED
	
	unsigned totalRingSize = 0;
	unsigned totalProduced = 0;
	//unsigned totalSize = 0;
	unsigned totalDropped = 0;
	unsigned totalConsumed = 0;
	int i = 0;
	ThreadRecord *p;
	for(p = g_threadrecordlist->head; p != NULL; p = p->next, i++){
		// Assume there is only one ring in each threadRecord.
		totalRingSize	+= p->pr->getSize();
		totalProduced	+= p->pCount;
		//totalSize	+= p->pSize;
		totalDropped	+= p->pDropped;
		totalConsumed	+= p->cCount;
		fprintf(fp, "Thread record NO.%d: threadID %lu, ringSize %u, produced \
			%u, dropped %u, consumed %u\n", 
			i+1, (unsigned long)(p->threadID), p->pr->getSize(), p->pCount, 
			p->pDropped, p->cCount);
	}
	fprintf(fp, "Total ring size %u, total allocated %u chunks, dropped %u, \
		transmitted %u\n", 
		totalRingSize, totalProduced, totalDropped, totalConsumed);
#endif // EXP


#ifdef SINGLE_EXP
	fprintf(fp, "malloc %u, realloc %u, calloc %u, free %u\n", 
		g_malloc_count, g_realloc_count, g_calloc_count, g_free_count);
#endif

#ifdef EXP
	if(fp != stderr)
		fclose(fp); // Any output to the file should be before this line
#endif

	// Set the flag to notify the deliver/monitor threads to end.
	g_exit_procedure = EXIT_HOOKED; 

#ifdef MONITOR_EXIT
	unsigned int waitBegin = getUsTime();
	while(g_exit_procedure != MONITOR_DONE && 
			(getUsTime() - waitBegin < 1000000U) )
		sleep(0);
#endif
}
	
/* The old exit logic
#ifdef CRUISER_DEBUG
	fprintf(stderr, "Request the monitor thread to stop\n");
	fprintf(stderr, "Exit thread id is %d\n", int(pthread_self()));
#endif
	__sync_bool_compare_and_swap(&g_stop, 0, 1);
	if( !pthread_equal(g_transmitter, pthread_t(0)) )
		pthread_join(g_transmitter, NULL);
	if(!pthread_equal( g_monitor, pthread_t(0) ) && 
		!pthread_equal( g_monitor, pthread_self() ) ){ // When attacks detected.
		pthread_join(g_monitor, NULL);
	}
#ifdef CRUISER_DEBUG
	fprintf(stderr, "Request finished\n");
#endif
*/

// malloc, realloc and calloc use afterMalloc to encapsulate the buffer:
// (assume p is the raw buffer's initial address)
// for lazy-cruiser:
// 	p[0] = p[end] = g_canary ^ user_requested_size_in_words; 
// 	p[1] = user_requested_size_in_words;
// for eager-cruiser:
// 	p[0] = id;
// 	p[1] = user_requested_size_in_words;
// 	p[end] = g_canary;
// Parameters 
//	addr: raw buffer's initial addr (not the address returned to user threads)
//	word_size: user requested size in words
inline void afterMalloc(void* addr, size_t word_size){
#ifdef CRUISER_DEBUG
	fprintf( stderr, "In afterMalloc, thread ID %lu, to protect real addr %p, \
			word_size %lu\n", (unsigned long)(pthread_self()), addr, word_size);
#endif	

	// Encapsulate the buffer
	unsigned long *p = (unsigned long *)addr;
#ifdef DELAYED
	p[1] = word_size;
	p[2 + word_size] = p[0] = (g_canary ^ word_size);// ^ (unsigned long)p;
	CruiserNode node;
	node.userAddr = p + 2;
#else //not DELAYED
	static unsigned long id = 0;
	p[1] = word_size;
	p[0] = ++id; // rand() might be better
	// 0 is a reserved value to indiate that the buffer has been freed.
	// For eager-cruiser, before a buffer is freed, p[0] is set to 0.
	// Actually, it is not necessary that we set it to 0
	// The monitor considers a buffer freed, as long as p[0] != the assigned id.
	if(__builtin_expect(!p[0], 0))
		p[0] = -1L;
	p[2 + word_size] = g_canary;
	CruiserNode node;
	node.userAddr = p + 2;
	node.ID = p[0];
#endif
	if(__builtin_expect(!t_threadRecord, 0)){
		// For mallocs in init() before "new g_threadrecordlist" is executed.
		// It is probably uncecessary, just in case
		if(__builtin_expect(!g_threadrecordlist, 0))
			return;
		t_threadRecord = g_threadrecordlist->getThreadRecord();
		if(!t_threadRecord)
			return;
	}

	t_threadRecord->produce(node);
}

inline static void beforeFree(void* addr){
	unsigned long *p = (unsigned long*)addr - 2;
#ifdef DELAYED

#ifdef CHECK_DUPLICATE_FREES
	if(__builtin_expect(p[0] == g_canary_free ^ p[1], 0)){
		fprintf(stderr, "Duplicate frees are detected\n");
		//todo: set error no.
		return;
	}
#endif // CHECK_DUPLICATE_FREES

	p[0] ^= (g_canary ^ g_canary_free); // p[0] = size_word ^ g_canary_free

#else // NOT DELAYTED

#ifdef CHECK_DUPLICATE_FREES
	if(!p[0]){
		fprintf(stderr, "Duplicate frees are detected\n");
		return true;
	}
#endif // CHECK_DUPLICATE_FREES

	size_t word_size = p[1];
	unsigned long canary = p[2 + word_size];
	if(__builtin_expect(canary != g_canary, 0)){
		attackDetected(addr, 1);
	}
	p[0] = 0;

#endif // DELAYED
}

static void* malloc_wrapper(size_t size){
	if(__builtin_expect(!g_initialized, 0))
		init();
		
	if(__builtin_expect(!t_protect, 0)){
		void*p = original_malloc(size);
#ifdef CRUISER_DEBUG
		fprintf( stderr, "%p malloc nonprotected by %lu size = %lu\n", 
			p, (unsigned long)(pthread_self()), size);
#endif
		return p;
	}

#ifdef SINGLE_EXP
	g_malloc_count++;
#endif

	// Adjust the size so that the location for canary is word alignmet.
	// For example, in 32-bit syste, if the user requested size is 11, 
	// we adjust it to 12, and the word_size is 3.
	size_t word_size = size / sizeof(long) + (size%sizeof(long)?1:0);
	void *addr = original_malloc((word_size + EXTRA_WORDS) * sizeof(long));

#ifdef CRUISER_DEBUG
	fprintf( stderr, "%p malloc protected by thread %lu, word_size = %lu\n", 
		addr, (unsigned long)(pthread_self()), word_size );
#endif

	if(__builtin_expect(!addr, 0))
		return NULL;
	afterMalloc(addr, word_size);
	return (long*)addr + 2;
}

static void free_wrapper(void* addr){
#ifdef CRUISER_DEBUG
	fprintf( stderr, "%p(real addr) will be freed by %lu, t_protect = %d\n", 
		(char*)addr, (unsigned long)(pthread_self()), t_protect);
#endif

	if(__builtin_expect(!addr, 0))
		return;

// // It is commented out, as the "if" is never true.
// The first buffer due to the calloc call is allocated using mmap.
//	if(__builtin_expect(addr == g_mapped_addr, 0)){
//		munmap(p, p[1] + EXTRA_SIZE);
//		fprintf(stderr, "caught first calloced buffer %p\n", p);
//		return;
//	}		
	
	if(!t_protect){
#ifdef CRUISER_DEBUG
		fprintf( stderr, "real addr %p will be freed by %lu non-protectedn\n", 
			addr, (unsigned long)(pthread_self()));
#endif
		original_free(addr);
		return;
	}	

	// The logic dealing with fork() is put here rather than malloc, 
	// because malloc is too heavy.
#ifdef APACHE
	pid_t  g_pid_copy = g_pid;
	pid_t  current_pid = getpid();
	if(__builtin_expect(g_pid_copy != current_pid, 0)){	
		if(__sync_bool_compare_and_swap(&g_pid, g_pid_copy, current_pid)){	
#ifdef EXP
			FILE *fp = fopen("~/cruiser.log", "a");
			if (!fp)
				fp = stderr;
			fprintf(fp, "Process fork detected %s, parent %lu, child %lu\n", 
				program_invocation_name, (unsigned long)g_pid_copy, 
				(unsigned long)g_pid);	
			if(fp!=stderr)
				fclose(fp);
#endif
			g_init_begin_time = getUsTime();
			g_exit_procedure = RUNNING;
			// Create the monitor thread, which will create the transmitter 
			int thread_ret = pthread_create(&g_monitor, NULL, monitor, NULL);
			if(thread_ret){
				fprintf(stderr, "Error: monitor thread cannote be created, \
					return value is %d\n", thread_ret);
				exit(-1);
			}
		}
	}
#endif //APACHE

#ifdef SINGLE_EXP
	g_free_count++;
#endif


#ifdef CRUISER_DEBUG
	fprintf( stderr, "real addr %p will be freed (after check) protected by \
			%lu\n\n", (long*)addr - 2, (unsigned long)(pthread_self()));
#endif
	beforeFree(addr);

#ifndef DELAYED
	original_free( (unsigned long*)addr - 2);
#endif //DELAYED
	return;
}

static void* realloc_wrapper(void *addr, size_t new_size){
	if( __builtin_expect(!t_protect, 0) )
		return original_realloc(addr, new_size);	

	if(__builtin_expect(!new_size, 0)){
		free_wrapper(addr);
		return NULL;
   	}

	if(__builtin_expect(!addr, 0))
		return malloc_wrapper(new_size);

	// The accounting logic is put here because if it is before the lines above, 
	// then when new_size == 0 or addr ==0, the accounting would be duplicate 
	// with that in free or malloc
#ifdef SINGLE_EXP
	g_realloc_count++;
#endif

#ifdef CRUISER_DEBUG
	fprintf( stderr, "realloc from user addr %p by %lu size = %lu t_protect \
		= %d\n", addr, (unsigned long)(pthread_self()), new_size, t_protect);
#endif

	size_t volatile new_word_size = new_size / sizeof(long) + 
		(new_size%sizeof(long)?1:0);

#ifdef DELAYED
	volatile unsigned long *p = (unsigned long*)addr - 2;
	size_t word_size = p[1];

	if(word_size == new_word_size){
		return addr;
	}
	else if(word_size > new_size){
		// Set the realloc flag, which is observed by the monitor; 
		// A more secure flag: p[0] = p[0] ^ g_canary ^ g_canary_realloc
		p[0] = g_canary_realloc; 
		p[1] = new_word_size;
		p[2 + new_word_size] = (volatile unsigned long)(g_canary ^ new_word_size);
		p[0] = (volatile unsigned long)(g_canary ^ new_word_size);
		return addr;
	}
	else{ // word_size < new_size
		// Different from the free logic which simply call beforeFree to change 
		// p[0], realloc needs to use word_size , so it has to make sure 
		// word_size is not corrupted
		if(__builtin_expect(p[0] != (g_canary ^ word_size), 0)){
			fprintf(stderr, "Attack info: addr(user) %p, p[0] 0x%lx, p[1] 0x%lx\
				,p[end] 0x%lx, expected_canary 0x%lx, canary_free 0x%lx", 
				addr, p[0], p[1], p[2 + word_size], g_canary ^ word_size, 
				g_canary_free ^ word_size);
			attackDetected(addr, 2);
			return NULL;
		}
		unsigned long* new_buffer = (unsigned long*)original_malloc(
			(new_word_size + EXTRA_WORDS) * sizeof(long));
		if(__builtin_expect(!new_buffer, 0))
			return NULL;
		memcpy(new_buffer+2, addr, (word_size < new_word_size ? word_size : 
			new_word_size) * sizeof(long));
		afterMalloc(new_buffer, new_word_size);
		beforeFree(addr);
		return new_buffer + 2;
	}

#else  //not DELAYED
	// Call beforeFree to check the buffer and change the buffer ID, so that 
	// the monitor thread will delete the corresponding cruiser node.
	beforeFree(addr); 
	unsigned long *new_buffer = (unsigned long*)original_realloc( 
		(unsigned long*)addr - 2, (new_word_size + EXTRA_WORDS) * sizeof(long));
	if(__builtin_expect(!new_buffer, 0))
		return NULL;
	afterMalloc(new_buffer, new_word_size);
	return new_buffer + 2;
#endif //DELAYED
}


// calloc is called by dlysm before the original calloc function pointer is 
// retrieved, while calloc is hooked by our lib, so a naive calloc has to be 
// implemented and used before original_calloc can be used. So calloc_wrapper
// uses mmap for allocation before original_calloc is retrieved. It seems that
// we should mark the buffer, so that we can use unmap to deallocate it. 
// However, actually it is never deallocated. So we don't bother to mark it.
static void* calloc_wrapper(size_t nobj, size_t size){
	//size_t aligned_size = (nobj * size + 3) & (-4);
	size_t word_size = (nobj*size) / sizeof(long) + 
		((nobj*size)%sizeof(long)?1:0);
	// The assert below will fail, when the original_calloc is not retrieved
	// yet by dlsym, while dlsym calls calloc.
	// assert(original_calloc); 
	if(__builtin_expect(!original_calloc, 0)){
		unsigned long *q = (unsigned long *)mmap(NULL, (word_size + EXTRA_WORDS) 
			* sizeof(long), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 
			-1, 0);
#ifdef DELAYED
		// Don't call afterMalloc, because cruiserRing is not ready. 
		// Since the buffer is not monitored, the monitor thread does not need 
		// to take care of the "delayed free" for the buffer.
		q[1] = word_size; // markMap(aligned_size) is not used.
		q[0] = q[2 + word_size] = (g_canary ^ word_size);
#else // not delayed
		q[0] = -1L; // ~0;
		q[1] = word_size;
		q[2 + word_size] = g_canary;
#endif // DELAYED
		//g_mapped_addr = (char*)p + 8;
		return q + 2;
	}

	if(__builtin_expect(!t_protect, 0)){
		return original_calloc(nobj, size);
	}

#if defined(SINGLE_EXP)
	g_calloc_count++;
#endif
	
	void *p = original_calloc(word_size + EXTRA_WORDS, sizeof(long));
	if(__builtin_expect(!p, 0))
		return NULL;
	afterMalloc(p, word_size);
	return (long*)p + 2;
}

}//namespace cruiser

extern "C"{
// The reason I don't use _malloc_hook is that you need to recover the original 
// hook to call the real malloc, which is not thread-safe.
// The reason I implement malloc_wrapper is that I want to keep all global 
// variables and functions in cruiser namespace and refer to them
// only through malloc_wrapper. Maybe this is unnecessary, as we can achieve 
// this by declaring "using namespace cruiser" in this file.
void* malloc(size_t size){
	//In all the wrappers, if (t_protect == 0), original functions are called
	return cruiser::malloc_wrapper(size);
}

void free(void* addr){
	cruiser::free_wrapper(addr);
}

void* realloc(void *ptr, size_t size){
	return cruiser::realloc_wrapper(ptr, size);
}

void* calloc(size_t nobj, size_t size){
	return cruiser::calloc_wrapper(nobj, size);
}

// This would incur segmental fault; the reason is that the original_malloc
// is not ready, when calloc is invoked by dlsym.
//
// void * calloc(size_t n, size_t sz){
//	//fprintf(stderr, "In calloc\n");
//    void * addr = cruiser::malloc_wrapper(n*sz);
//    assert(addr);
//    memset(addr,0,n*sz);
//    return addr;
// }

}//extern "C"
