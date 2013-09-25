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

#ifndef CRUISER_H
#define CRUISER_H

#include <pthread.h>
#include "utility.h"
// The compiler complains that the file below cannot be found.
// sysconf(_SC_LEVEL1_DCACHE_LINESIZE) or getconf LEVEL1_DACHE_LINESIZE may work.
// #include <include/asm-x86/cache.h> //for L1_CACHE_BYTES. 
#ifndef L1_CACHE_BYTES
#define L1_CACHE_BYTES  64 // Double check your system!!
#endif

namespace cruiser{

#define	EXTRA_WORDS 3 // We use three extra words to encapsulate a buffer.

// We use "struct" instead of "class" to avoid invoking the constructor.
struct CruiserNode{
	void			*userAddr;
#ifndef DELAYED // ID is only needed for eager-Cruiser
	unsigned long volatile	ID; 
#endif
};

// The abstract structure for storing CruiserNodes.
// It can be a hashtable, and in the paper, it is a CruiserList.
class NodeContainer{
public:
	virtual ~NodeContainer(){}
	// Invoked by the transmitter/deliver thread.
	virtual bool insert(const CruiserNode &) = 0;
	
	// Inovked by the monitor thread.
	//return values: 
	//	0: to stop monitor (the feature is not enabled to avoid exploit).
	//	1: finished one round of traverse.
	//	2: encountered the section boundary (not used).
	virtual int traverse( int (*pfn)(const CruiserNode &) ) = 0;
};

// Cruiser responds to the process exit following a finite-state machine
enum   EXIT_PROCEDURE		{RUNNING, EXIT_HOOKED, TRANSMITTER_BEGIN, 
				TRANSMITTER_DONE, MONITOR_BEGIN, MONITOR_DONE};

// How should cruiser respond to a detected buffer overflow?
enum   PRO_ATTACK 		{TO_ABORT, TO_EXIT, TO_GOON};

// "static" below is used to avoid global naming pollution, which is 
// unncecessary, as we have used the namespace.

static EXIT_PROCEDURE volatile 	g_exit_procedure = RUNNING;
static const PRO_ATTACK			g_pro_attack = TO_ABORT;
// g_initialized:
//	0: init() has not bee invoked
//	1: init() has been invoked
//	2: init() has finished
static int	volatile			g_initialized = 0;
// The first several malloc calls may be issued before g_canary is initialized,
// if the line below is "const unsigned long g_canary = rand()". So, we set up
// g_canary when the first malloc call is hooked.
static unsigned long 			g_canary; 
#ifdef DELAYED
static unsigned long			g_canary_free;
static unsigned long			g_canary_realloc;
#endif //DELAYED
static pthread_t 				g_monitor; // The monitor thread ID
static pthread_t				g_transmitter; // The transmitter thread ID
static NodeContainer			*g_nodeContainer = NULL;

static unsigned					g_init_begin_time;

typedef void*					(*malloc_type)(size_t);
typedef void					(*free_type)(void*);
typedef void*					(*realloc_type)(void*, size_t);
typedef void*					(*calloc_type)(size_t, size_t);
static malloc_type				original_malloc = NULL;
static free_type				original_free	= NULL;
static realloc_type				original_realloc= NULL;
static calloc_type				original_calloc = NULL;

static volatile pid_t 			g_pid;

// The variables above keep unchanged most of the time.
static char			cache_pad2[L1_CACHE_BYTES]; // Used to avoid false sharing
// The variables below keep changing.

// If the transmitter goes through the list of cruiser rings but doesn't get to 
// "consume" any ring elements, the program is considered "still".
// So the transmitter may consider go to sleep for a while.
static unsigned int volatile	g_transmitter_still_count;

// Only make sense for single-threaded process, as they are not counted
// in a thread-safe way. "volatile" is probably unncecssary
#ifdef SINGLE_EXP 
static unsigned	volatile		g_malloc_count;
static unsigned	volatile		g_calloc_count;
static unsigned	volatile		g_realloc_count;
static unsigned	volatile		g_free_count;
#endif //SINGLE_EXP

#ifdef DELAYED
#ifdef EXP // for experiment/measurement purpose
// The monitor thread traverses the list once, the round count increments.
static unsigned long			g_roundCount; 
static unsigned long long		g_totalCheckCount;		
static unsigned					g_maxRoundBufferCount;
static double 					g_avgRoundBufferCount;

// If in the last round a buffer was not freed, it is considered live; 
static double 					g_avgLiveBufferCount;
static double 					g_avgLiveBufferSize;
static unsigned 				g_maxLiveBufferCount;
static unsigned					g_maxLiveBufferSize;

// otherwise, the buffer's deallocation is considered delayed. 
static double 					g_avgDelayedBufferCount;
static double 					g_avgDelayedBufferSize;
static unsigned					g_maxDelayedBufferCount;
static unsigned					g_maxDelayedBufferSize;

// Counted in processNode()
static unsigned					g_delayedBufferSize; 
static unsigned 				g_roundBufferCount;  
static unsigned 				g_roundBufferSize;
#endif //EXP
static unsigned					g_delayedBufferCount;

#else //DELAYED
// Most of time, these variable are only manipulated by the monitor thread. 
// They are retrieved/read by the user thread at exit only, 
// so "volatile" is not needed.
#ifdef EXP 
static unsigned	long			g_roundCount;
static unsigned	long long		g_totalCheckCount;
static unsigned					g_maxRoundBufferCount;
static double					g_avgRoundBufferCount;

static double					g_avgLiveBufferCount;
static unsigned 				g_maxLiveBufferCount;

static double	 				g_avgSignalBufferCount;
static unsigned 				g_maxSignalBufferCount;

static unsigned					g_roundBufferCount;
static unsigned 				g_signalBufferCount;
#endif

static unsigned					g_liveBufferCount;
static struct sigaction 		g_oact; // Old sigaction.
static jmp_buf					g_jmp;
#endif //DELAYED


}//namespace cruiser

#endif //CRUISER_H
