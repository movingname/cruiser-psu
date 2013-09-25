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
 * File name: simpleTest.cpp
 * Description: a multi-threaded program allocating/deallocating buffers.
 * 	You can use it to test the performance of Cruiser in the worst case.
 * Usage: ./simpleTest.out [thread number] [execution times] (default is 20 1)
 ***************************************************************************/

#include <stdio.h> // printf
#include <stdlib.h> // malloc/free
#include <pthread.h> // pthread_*
#include <sys/time.h> // gettimeofday

#define debug_printf printf
#define baseTimes	200
#define maxSize		100000
#define step		1000
int		inputTimes;

double GetMSTime(void)
{
  struct timeval stNowTime;

  gettimeofday(&stNowTime,NULL);

  return (1.0*(stNowTime.tv_sec)*1000+1.0*(stNowTime.tv_usec)/1000);
}

static void* heapOperationThread(void*){
	void ** pHeapArray = (void**)malloc( (maxSize/step) * sizeof(void*) );
	int i, size, k;
	for(i = 0; i < inputTimes; i++){
		debug_printf("\n\n**************Repeat %d begins**************\n", i);
		for(size= 0, k=0; size < maxSize; size+=step, k++){
			//pHeapArray[k] = Malloc(1);
			pHeapArray[k] = malloc(size);
			debug_printf("Allocated chunk %d for %d bytes at %p\n", 
				k, size, pHeapArray[k]);
			if(k%2){
				free(pHeapArray[k]);
				debug_printf("Freed chunk %d for %d bytes at %p\n", 
					k, size, pHeapArray[k]);
			}
		}
		//if(i != inputTimes-1){ 
			for(size = 0, k = 0; size < maxSize; size+=step, k++){
				if(!(k%2)){
					free(pHeapArray[k]);
					debug_printf("Freed chunk %d for %d bytes at %p\n", 
						k, size, pHeapArray[k]);
				}
			}
		//}
		debug_printf("**************Repeat %d ends**************\n\n", i);
	}
	free((void*)pHeapArray);
	return 0;
}

int main(int argc, char ** argv){
	int threadNumber	= (argc >= 2)? atoi(argv[1]) : 20;
	inputTimes			= (argc >= 3)? atoi(argv[2]) : 1;
	inputTimes			*= baseTimes;

	pthread_t * threadPool = (pthread_t*)malloc(sizeof(pthread_t)*threadNumber);
	// To measure the duration
	printf("Start...................\n\n");
	double	start, finish, duration;
	int		i;

	start = GetMSTime();

	for(i = 0; i < threadNumber; ++i){
		if(pthread_create(threadPool+i, NULL, heapOperationThread, NULL))
			printf("Error: thread %d cannote be created\n", i);
	}
	//WaitForMultipleObjects(threadNumber, threadPool, true, INFINITE);
	for(i = 0; i < threadNumber; ++i){
	//	CloseHandle(threadPool[i]);
		pthread_join(threadPool[i], NULL);
	}

	finish = GetMSTime();

	duration = (double)(finish - start) / 1000;//ms->s
	printf("End.......................\n\n");
	printf("The duration is %.2fs\n", duration);
	//printf("Input any character to proceed\n");
	//getchar();

	free(threadPool);

	return 0;
}
