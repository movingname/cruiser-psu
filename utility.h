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

#ifndef UTILITY_H
#define UTILITY_H

#include <execinfo.h> // backtrace
#include <stdio.h>
#include <stdlib.h> // rand, exit, malloc, free
#include <sys/time.h> // gettimeofday
#include <assert.h>

namespace cruiser{
#if defined( NPROTECT ) 
static __thread int		t_protect 		= 0;
#else
static __thread int		t_protect 		= 1;
#endif
	
// Obtain a backtrace and print it to stdout.
static  void print_trace (void){
	int _old_protect = t_protect;
	t_protect = 0;
	
	void *array[20];
	size_t size;
	char **strings;
	size_t i;
     
	size = backtrace (array, sizeof array/sizeof array[0]);
	strings = backtrace_symbols (array, size);
     
	fprintf (stderr, "Obtained %lu stack frames.\n", size);
     
	for (i = 0; i < size; i++)
		fprintf (stderr, "%s\n", strings[i]);
 
    free (strings);
    
    t_protect = _old_protect;
}

// Because assert would potentially call malloc, to avoid recursive calls, 
// t_protect is set as zero transiently.
#ifdef CRUISER_DEBUG
#define ASSERT(x)  do { \
	int _old_protect = t_protect; \
	t_protect = 0; \
	assert(x); \
	t_protect = _old_protect; \
	} while(0) //this is used to "swallow" the semicolon.
#else
#define ASSERT(x) //assert(x)
#endif

inline static unsigned int getUsTime(void){
	struct timeval tv;
	gettimeofday(&tv,NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

static void msSleep(int msTime){
	if(msTime == -1)
		return;
	if(msTime > 999)
		msTime = 999;
	struct timespec	sleepTime;
	sleepTime.tv_sec= 0;
	sleepTime.tv_nsec= msTime*1000000;
	nanosleep(&sleepTime, NULL);
}

}//namespace cruiser

#endif //UTILITY_H
