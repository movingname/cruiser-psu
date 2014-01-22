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

#ifndef MONITOR_H
#define MONITOR_H

#include <unistd.h> //sleep
#include <signal.h> //sigaction
#include <errno.h> //program_invocation_name, ESRCH
//#include <string.h> //strerrno
#include <dlfcn.h> //dlsym
#include <setjmp.h> //siglongjmp
#include <time.h> //nanosleep
// Note that this hook cannot be used to achieve initialization because it can
// be overridden by user code. Besides, when __malloc_initialize_hook is invoked
// is implementation dependent, e.g. it may not be called until "malloc" is
// invoked for the first time; in this case, the initialization would be too
// late, as the first malloc is not handled by Cruiser correctly.
#include <malloc.h> //__malloc_initialize_hook

#include "thread_record.h"
//#ifdef AMINO_HASHTABLE
//#include "amino_plus.h" // amino provides a lock-free hash table
//#else
#include "list.h"
//#endif

namespace cruiser{
static void* monitor(void *);
static void* transmitter(void*);
static int processNode(const CruiserNode &);
//static void beforeExit(void); move to thread_record.h
//static void SIGSEGV_handler(int signo);

// The functions marking the size field of a buffer are commented out.
// Previously, we mark the size field to label a buffer encapsulated by Cruiser.
// So that we can distinguish such buffers from buffers not encapsulated, and
// deallocate them accordingly. Then we change to use a simple logic:
// if t_protect == 0, the buffer is not encapsulated, and we use original free;
// otherwise, the buffer is regarded as an encapsulted one.
//
// The set of funcitons are put here rather than memory.cpp because processNode
// needs them also, while this file will be included by memory.cpp. Marking the
// highest bit means the chunk allocation has been intercepted by cruiser, so
// should also be freed in cruiser way.
//
// #define MSB 0x80000000
// #define LSB 0x1
//
// // To distinguish cruiser-buffers and original-buffers
// inline static size_t markSize(size_t size){
//	return size | MSB;
// }
//
// inline static bool isMarkedSize(size_t size){
//	return size & MSB;
// }
//
// inline static size_t unmarkSize(size_t size){
//	return size & ~MSB;
// }
//
//
// // To mark the early buffers allocated using mmap
// inline static size_t markMap(size_t size){
//	return size | LSB;
// }
//
// inline static bool isMarkedMap(size_t size){
//	return size & LSB;
// }
//
// inline static size_t unmarkMap(size_t size){
//	return size & ~LSB;
//}


#ifndef DELAYED
// As the buffer is directly freed by the user thread, while the monitor thread
// may still access the memory, the access may lead to the SIGSEGV signal.
// The handler is used to handle the signal.
static void SIGSEGV_handler(int signo){
	if(pthread_self() == g_monitor){
#ifdef CRUISER_DEBUG
		fprintf(stderr, "SIGSEGV is caught\n");
#endif

#ifdef EXP
		g_signalBufferCount++;
#endif
		siglongjmp(g_jmp, 1);
	}
	else{
		// TODO: at the moment, SIGSEGV is masked so even we raise SIGSEGV,
		// DEFAULT action will be adopted while the user defined handler is thus
		// ignored. So the SIGSEGV needs to be unmasked.
		//if(sigaction(SIGSEGV, &g_oact, NULL) < 0)
		//	abort();
		raise(SIGSEGV);
	}
}
#endif //DELAYED

void* monitor(void *){ // "void* foo(void)" interface for a thread function.
	// malloc/free calls issued by the monitor thread should not be hooked??
	t_protect = 0;
#ifdef CRUISER_DEBUG
	fprintf( stderr, "Monitor thread id: %lu\n",
		(unsigned long)(pthread_self()));
#endif
	if(!g_nodeContainer){
//#ifdef AMINO_HASHTABLE
//		g_nodeContainer = new Hashtable;
		//ASSERT(g_nodeContainer);
//#else
		g_nodeContainer = new List;
//#endif
	}

	int roundMsSleep = -1;
	char *strMsSleep = getenv("CRUISER_SLEEP");
	if(strMsSleep)
		roundMsSleep = atoi(strMsSleep);

	g_transmitter_still_count = 0;
	if(int thread_ret = pthread_create(&g_transmitter, NULL, transmitter, NULL)){
		fprintf(stderr, "Error: transmitter thread cannote be created, return \
						value is %d\n", thread_ret);
		delete g_nodeContainer;
		exit(-1);
	}

	while(g_initialized != 2)
		sleep(0);

#ifdef EXP
	pid_t processID = getpid();
	pthread_t threadID = pthread_self();
	FILE *fp = fopen("cruiser.log", "a");
	if (!fp)
		fp = stderr;

	fprintf(fp, "Monitor thread:%s (pid %lu, tid %lu), init duration %u\n",
			program_invocation_name, (unsigned long)processID,
			(unsigned long)threadID, getUsTime() - g_init_begin_time);
	fflush(fp);
#endif //EXP

	// If no buffer is released in the last round, staticCount++;
	// if its value is larger than SLEEP_CONDITION, go to sleep
	// TODO: consider a better indicator for going asleep
	unsigned staticCount = 0;
	#define SLEEP_CONDITION 10

#ifdef DELAYED	//VERY LONG

#ifdef EXP
	g_roundCount = g_totalCheckCount = 0;
	g_maxRoundBufferCount = g_avgRoundBufferCount = 0;
	g_avgLiveBufferCount = g_avgLiveBufferSize = 0;
	g_maxLiveBufferCount = g_maxLiveBufferSize = 0;
	g_avgDelayedBufferCount = g_avgDelayedBufferSize = 0;
	g_maxDelayedBufferCount = g_maxDelayedBufferSize = 0;
#endif //EXP


#ifdef SINGLE_EXP
	g_malloc_count = g_free_count = g_calloc_count = g_realloc_count = 0;
#endif

	// Recall the return values of NodeContainer::traverse():
	//	0: to stop monitor (the feature is not enabled to avoid exploit).
	//	1: finished one round of traverse.
	//	2: encountered the section boundary (not used).

	// The coding style is a little bit ugly, just I don't want to write
	// "g_delayedBufferCount = 0" multiple times inside the loop body.
	while((g_delayedBufferCount = 0, g_nodeContainer->traverse(processNode))){
//#ifdef EXP
//		unsigned int nodeContainerLen = 0;
//		if(g_totalCheckCount != lastTotalCheckCount){
//			g_roundCount++;
//			nodeContainerLen = g_totalCheckCount - lastTotalCheckCount;
//			if( nodeContainerLen >= g_maxNodeContainer )
//				g_maxNodeContainer = nodeContainerLen;
//			lastTotalCheckCount = g_totalCheckCount;
//		}
//#endif

#ifdef EXP
		if(g_roundBufferCount){
			//total
			g_roundCount++;
			g_totalCheckCount += g_roundBufferCount;
			if(g_roundBufferCount > g_maxRoundBufferCount)
				g_maxRoundBufferCount = g_roundBufferCount;
			g_avgRoundBufferCount = ((g_roundCount - 1) * g_avgRoundBufferCount
									+ g_roundBufferCount) / g_roundCount;

			//live
			unsigned liveBufferCount = g_roundBufferCount - g_delayedBufferCount;
			unsigned liveBufferSize = g_roundBufferSize - g_delayedBufferSize;
			g_avgLiveBufferCount = ((g_roundCount - 1) * g_avgLiveBufferCount
									+ liveBufferCount) / g_roundCount;
			g_avgLiveBufferSize = ((g_roundCount - 1) * g_avgLiveBufferSize
									+ liveBufferSize) / g_roundCount;
			if(liveBufferCount > g_maxLiveBufferCount){
				g_maxLiveBufferCount = liveBufferCount;
			}
			if(liveBufferSize > g_maxLiveBufferSize)
				g_maxLiveBufferSize = liveBufferSize;

			//delayed
			g_avgDelayedBufferCount = ((g_roundCount - 1) *
				g_avgDelayedBufferCount + g_delayedBufferCount) / g_roundCount;
			g_avgDelayedBufferSize = ((g_roundCount - 1) *
				g_avgDelayedBufferSize + g_delayedBufferSize) / g_roundCount;
			if(g_delayedBufferCount > g_maxDelayedBufferCount)
				g_maxDelayedBufferCount = g_delayedBufferCount;
			if(g_delayedBufferSize > g_maxDelayedBufferSize)
				g_maxDelayedBufferSize = g_delayedBufferSize;
		}

//#ifdef APACHE
		//fprintf(fp, "Monitor thread:%s (pid %u, thread id %u), \
		// g_roundCount %u, duration %u\n", program_invocation_name, processID,
		// threadID, g_roundCount, getUsTime() - g_init_begin_time);
		//fflush(fp);
//#endif //APACHE

		g_delayedBufferSize = g_roundBufferCount = g_roundBufferSize = 0;
#endif //EXP


//#ifdef MONITOR_EXIT
		// The purpose is to perform one more round of traverse at exit.
		// It is critical to ensure checking the last second overflow.
		if(g_exit_procedure == TRANSMITTER_DONE){
			g_exit_procedure = MONITOR_BEGIN;
			continue;
		}else if(g_exit_procedure == MONITOR_BEGIN){
			g_exit_procedure = MONITOR_DONE;
			break;
		}
//#endif //MONITOR_EXIT

#ifdef APACHE
		// No allocation or deallocation, which indicates the apache server is
		// pretty inactive, so why don't go asleep for a while.
		if(g_transmitter_still_count && !g_delayedBufferCount){
			if(++staticCount > SLEEP_CONDITION)
				msSleep(1);
		}
		else
			staticCount = 0;
#endif //APACHE

		// The per-round sleep is different from the conditional sleep above.
		//if(sleepEnable)
		//	nanosleep(&sleepTime, NULL);
		if(roundMsSleep != -1)
			msSleep(roundMsSleep);
	}


#else //DELAYED


	// The SIGSEGV handler works under the assumption that user code does NOT
	// install a new SIGSEGV handler.
	struct sigaction nact;
	nact.sa_handler = SIGSEGV_handler;
	nact.sa_flags = 0;
	sigemptyset(&nact.sa_mask);
	if(sigaction(SIGSEGV, &nact, &g_oact) < 0 ){
		printf("sigaction error\n");
		exit(-1);
	}

#ifdef EXP
	g_roundCount = g_totalCheckCount = 0;
	g_maxRoundBufferCount = g_avgRoundBufferCount = 0;
	g_avgLiveBufferCount = g_maxLiveBufferCount = 0;
	g_avgSignalBufferCount = g_maxSignalBufferCount = 0;
#endif //EXP

#ifdef SINGLE_EXP
	g_malloc_count = g_free_count = g_calloc_count = g_realloc_count = 0;
#endif

	unsigned long lastLiveCount = 0;
	while( (g_liveBufferCount = 0, g_nodeContainer->traverse(processNode)) ){
#ifdef EXP
		if(g_roundBufferCount){
			// The size statistics below is commented out, because the size
			// field may have been corrupted, so the information is inaccurate.

			//total
			g_roundCount++;
			g_totalCheckCount += g_roundBufferCount;
			if(g_roundBufferCount > g_maxRoundBufferCount)
				g_maxRoundBufferCount = g_roundBufferCount;
			g_avgRoundBufferCount = ((g_roundCount - 1) * g_avgRoundBufferCount
									+ g_roundBufferCount) / g_roundCount;

			//live
			g_avgLiveBufferCount = ((g_roundCount - 1) * g_avgLiveBufferCount
									+ g_liveBufferCount) / g_roundCount;
			//g_avgLiveBufferSize = ((g_roundCount - 1) * g_avgLiveBufferSize
			//						+ liveBufferSize) / g_roundCount;
			if(g_liveBufferCount > g_maxLiveBufferCount)
				g_maxLiveBufferCount = g_liveBufferCount;
			//if(liveBufferSize > g_maxLiveBufferSize)
			//	g_maxLiveBufferSize = liveBufferSize;

			//buffers that trigger SIGSEGV signals
			g_avgSignalBufferCount = ((g_roundCount - 1) *
				g_avgSignalBufferCount + g_signalBufferCount) / g_roundCount;
			//g_avgDelayedBufferSize = ((g_roundCount - 1) *
			//	g_avgDelayedBufferSize + g_delayedBufferSize) / g_roundCount;
			if(g_signalBufferCount > g_maxSignalBufferCount)
				g_maxSignalBufferCount = g_signalBufferCount;
			//if(g_delayedBufferSize > g_maxDelayedBufferSize)
			//	g_maxDelayedBufferSize = g_delayedBufferSize;
		}
		g_signalBufferCount = g_roundBufferCount = 0;
#endif //EXP

		if(g_exit_procedure == TRANSMITTER_DONE){
			g_exit_procedure = MONITOR_BEGIN;
			continue;
		}else if(g_exit_procedure == MONITOR_BEGIN){
			g_exit_procedure = MONITOR_DONE;
			break;
		}

#ifdef APACHE
		if(g_transmitter_still_count && lastLiveCount == g_liveBufferCount){
			if(++staticCount > SLEEP_CONDITION)
				msSleep(1);
		}
		else
			staticCount = 0;

#endif //APACHE

		lastLiveCount = g_liveBufferCount;

		if(roundMsSleep != -1)
			msSleep(roundMsSleep);
	}
#endif //DELAYED

	return NULL;
}

void* transmitter(void*){
	t_protect = 0;
#ifdef CRUISER_DEBUG
	fprintf( stderr, "Transimitter thread id is %lu\n", (unsigned long)(pthread_self()));
#endif

#ifdef EXP
	g_threadrecordlist->resetCount();//set procuded count and consumed count in each ring as zero
#endif
	unsigned long 		count = 0;
	unsigned long 		old_count;
	CruiserNode 		node;

	while(g_initialized != 2)
		sleep(0);

	while(true){
		//if(__builtin_expect(g_stop, 0)){
		//	return NULL;
		//}
		old_count = count;
		ThreadRecord *p;
		for(p = g_threadrecordlist->head; p != NULL; p = p->next){
			if( !p->threadID )
				continue;
			if(!p->consume(node)){
				if( pthread_kill( p->threadID, 0 ) == ESRCH ){
						//ESRCH: No thread could be found corresponding to that specified by the given thread ID.
						p->threadID = 0;
				}
			}else{
				do{
					ASSERT(node.userAddr);
					count++;
					if(node.userAddr)//Actually no need to judge, just in case.
						g_nodeContainer->insert(node);
				}while(p->consume(node));
			}
		}

//#ifdef MONITOR_EXIT
		if(g_exit_procedure == EXIT_HOOKED){
			g_exit_procedure = TRANSMITTER_BEGIN;
			continue;
		}else if(g_exit_procedure == TRANSMITTER_BEGIN){
			g_exit_procedure = TRANSMITTER_DONE;
			break;
		}
//#endif //MONITOR_EXIT

#ifdef APACHE
		if(old_count == count){
			if(++g_transmitter_still_count > SLEEP_CONDITION);
				msSleep(1);
		}
		else
			g_transmitter_still_count = 0;
#endif //APACHE
	}

	return NULL;
}

//to do: print more info. before abort.
static void attackDetected(void *user_addr, int reason){
	switch(reason){
		case 0:
			fprintf(stderr, "\nError: When monitor thread checks user chunk,\n");
			break;
		case 1:
			fprintf(stderr, "\nError: When free call checks user chunk,\n");
			break;
		case 2:
			fprintf(stderr, "\nError: When realloc call checks user chunk,\n");
			break;
		case 3:
			fprintf(stderr, "\nError: When realloc executes CAS,\n");
			break;
	}
	fprintf(stderr, "buffer overflow is detected at user address %p\n", user_addr);
	switch(g_pro_attack){
		case TO_ABORT:
			fprintf(stderr, "The process is going to abort due to an attack...\n");
			abort();
			break;
		case TO_EXIT:
			//todo: it would be better if we traverse the list for more attack detection before exit
			fprintf(stderr, "The process is going to exit due to an attack...\n");
			exit(-1);
			break;
		case TO_GOON:
		default:
			break;
	}
}


#ifdef DELAYED
// For lazy-cruiser.
// Checks the buffer whose address is contained in @node.
// This function is called in list::traverse() using a function pointer, as
// the amino hashtable's traverse accecpts a function pointer as the parameter.
// Returns:
//	0: to stop monitoring (disabled to avoid exploit);
//	1: have checked one node
//	2: have encountered a dummy node (should never happen)
//	3: have checked a node whose buffer has been freed.
int processNode(const CruiserNode & node){
	// // g_stop and pro-stop may be exploited, so it is not adopted.
	// if(__builtin_expect(g_stop, 0)){
	//	 return 0;
	// }
	// // The sleep inside processNode totally slows down the checking speed so
	// // it is not adopted
	//	if(g_sleepEnabled)
	//		nanosleep(&g_sleepTime, NULL);
	static int NOPCount = -1;
	if(NOPCount == -1){
#ifdef CRUISER_DEBUG
		fprintf(stderr, "Read NOPCount in processNode\n");
#endif
		char * strNOPCount = getenv("CRUISER_NOP");
   	 	if (strNOPCount)
			NOPCount = atoi(strNOPCount);
		else
			NOPCount = 0;
	}

	for(volatile int i = 0; i < NOPCount; i++)
		;

	void *addr = node.userAddr;
	if(__builtin_expect(!addr, 0)) // Dummy node
		return 2;

	// "volatile" ensures that "p[n]" is not optimized and the access order is
	// not adjusted by the comiler.
	unsigned long volatile *p = (unsigned long*)(addr) - 2;
	unsigned long volatile canary_left = p[0];
	if(canary_left == g_canary_realloc) // The buffer is being reallocated.
		return 1;

	volatile size_t word_size = p[1];

	// Double check p[0] to see whether it has changed since last read, this is
	// specifically for shrinking realloc, to make sure that the size (p[1]) is
	// paired with the canary (p[0]). In shinking realloc, p[0] is first set
	// to a flag value g_canary_realloc, then p[1] is set, then p[end],
	// finally p[0] is updated to get paired with p[1]. So if p[0] has changed,
	// we assume the buffer is under reallocation, and will check it in the next
	// round. WARNING: this may be exploited.
	if(p[0] != canary_left)
		return 1;

	unsigned long expected_canary = (g_canary ^ word_size);//^ (unsigned long)p;
	unsigned long canary_free = (g_canary_free ^ word_size);//^ (unsigned long)p;

#ifdef EXP
	g_roundBufferCount++; g_roundBufferSize +=  word_size;
#endif

#ifdef CRUISER_DEBUG
	fprintf(stderr, "\nprocessNode 0, user addr is %p, p[1] (word_size) 0x%lx \
		%lu is read\n", addr, word_size, word_size);
	fprintf(stderr, "processNode 1, user addr is %p, p[0] 0x%lx %lu and p[end] \
		0x%lx %lu are read; expected_canary 0x%lx %lu, canary_free 0x%lx %lu\n"
		, addr, canary_left, canary_left, p[2 + word_size], p[2 + word_size],
		expected_canary, expected_canary, canary_free, canary_free);
#endif

	if(canary_left == canary_free){
		if( p[2 + word_size] != expected_canary ){
//#ifdef CRUISER_DEBUG
			fprintf(stderr, "a buffer is overflowed then freed:\
				addr(user) %p, word_size=0x%lx, p[1]= 0x%lx, \
				p[0]= 0x%lx, p[end]=0x%lx, expected_canary=0x%lx\n",
				addr, word_size, p[1], p[0], p[2 + word_size], expected_canary);
//#endif
			attackDetected(addr, 0);
		}
#ifdef EXP
		g_delayedBufferSize +=  word_size;
#endif
		g_delayedBufferCount++;
		original_free((void*)p);
		return 3;
	}

	// if "canary_left != expected_canary" is false, "word_size" is intact.
	// so it can be used in "p[2 + word_size]" to read the end canary
	unsigned long end = -1L;
	if( canary_left != expected_canary ||
		(end = p[2 + word_size]) != expected_canary ){
//#ifdef CRUISER_DEBUG
		fprintf(stderr, "Normal check, attack warning: addr(not user) %p, \
			word_size=0x%lx, canary_left=0x%lx, p[1]= 0x%lx, p[0]= 0x%lx, \
			p[end]=0x%lx (~0 means it is not assigned yet), expected_canary\
			=0x%lx, exptected_canary_free=0x%lx\n",
			p, word_size, canary_left, p[1], p[0], end, expected_canary,
			canary_free);
//#endif
		attackDetected(addr, 0);
	}
	return 1;
}

#else // #ifndef DELAYED

// For eager-cruiser
int processNode(const CruiserNode & node){
	static int NOPCount = -1;
	if(NOPCount == -1){
#ifdef CRUISER_DEBUG
		fprintf(stderr, "Read NOPCount in processNode\n");
#endif
		char * strNOPCount = getenv("CRUISER_NOP");
   	 	if (strNOPCount)
			NOPCount = atoi(strNOPCount);
		else
			NOPCount = 0;
	}

	for(volatile int i = 0; i < NOPCount; i++)
		;

	if(__builtin_expect(!node.userAddr, 0)) // Dummy node
		return 2;

#ifdef EXP
	g_roundBufferCount++;
#endif

	if(sigsetjmp(g_jmp, 1)){
#ifdef CRUISER_DEBUG
		fprintf(stderr, "SIGSEGV, user addr %p\n", node.userAddr);
#endif
		return 3;
	}

#ifdef CRUISER_DEBUG
	fprintf(stderr, "(1) before p[0] is read, user addr is %p, \
		recoreded ID is %lu\n", node.userAddr, node.ID);
	sigaction(SIGSEGV, NULL, &g_oact);
	ASSERT(g_oact.sa_handler == SIGSEGV_handler);
#endif
	unsigned long volatile *p = (unsigned long*)(node.userAddr) - 2;
	// The first ID read is to check whether the buffer has been released;
	// if so, it makse no sense to check p[end]. It is not a must, but it
	// can reduce triggerring SIGSEGV due to accessing p[end] of a freed buffer.
	unsigned long volatile currentID = p[0];
	unsigned long ID = node.ID;
	if( currentID != ID ) // The buffer has been released or is to be soon.
		return 3;

#ifdef CRUISER_DEBUG
	fprintf(stderr, "(2) before p[1] is read, user addr is %p,\
					current ID is %lu\n", node.userAddr, currentID);
#endif

	size_t word_size = p[1];

	// Assume we read p[0] (ID) firstly and find the ID is intact (a), then
	// read p[end] (canary) (b) and find it corrupted; it is not necessarily due
	// to attacks. It's possible that free is called between (a) and (b) and the
	// canary can be modified in an arbitrary way. In order to avoid such false
	// positives, we retrieve the canary value first, then check ID, only if the
	// ID is intact (the buffer has not been freed), we check the canary.

#ifdef CRUISER_DEBUG
	fprintf(stderr, "(3) before p[end] is read, user addr is %p, canary_addr is \
					%p\n", node.userAddr, p+word_size);
#endif

	// As explained above, we retrieve the canary value first.
	unsigned long volatile canary = p[2 + word_size];

//#ifdef CRUISER_DEBUG
//	fprintf(stderr, "(4) before p[0] is read again, user addr is %p\n",
// 		node.userAddr);
//#endif

	// Read and check p[0] to make sure when the canary was read, the buffer was
	// not released yet.
	currentID = p[0];
	if(ID != currentID)
		return 3;
#ifdef EXP
	g_liveBufferCount++;
#endif

	if(canary != g_canary)
		attackDetected((void*)(p+2), 0);

	return 1;
}
#endif //DELAYED
}//namespace cruiser
#endif //MONITOR_H
