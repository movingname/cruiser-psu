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

#ifndef THREAD_RECORD_H
#define THREAD_RECORD_H

#include "common.h"

namespace cruiser{

#define RING_SIZE 1024u
#define MAX_RING_SIZE 1u<<22

// The ring algorithm is based on the the Hong Kong ring paper.
// Compared to a traditional ring,
// (1) the producer has a local variable ci_snapshot, so that it accesses
// the consumer index (ci) less frequently; and
// (2) the producer and consumer variables reside in different cachelines,
// to mitigate the false sharing.
// But it is not exactly the same as the Hong Kong paper:
// (1) we don't update the indexes by batch to minimize overflow checking delay,
// so the producer and consumer may operate on the same cacheline of the ring 
// (2) pi is put together with ci_snpashot. ci_snapshot is updated less 
// frequently than pi (by the producer), so though pi is read by the consumer 
// while ci_snapshot is updated by the producer, it 
// does not increase the forced memory copy of the cacheline. 
class Ring{
public:	
	//Using padding to avoid false sharing between different processors.
	char			cache_pad0[L1_CACHE_BYTES];
	CruiserNode		*array;
	unsigned	 	ringSize;
	Ring			*next;
	char			cache_pad1[L1_CACHE_BYTES - 3 * sizeof(int*)];
	unsigned 		volatile pi; // producer index
	unsigned		ci_snapshot;
	char			cache_pad2[L1_CACHE_BYTES -2 * sizeof(int)];
	unsigned 		volatile ci; //consumer index
	unsigned		pi_snapshot;
	char			cache_pad3[L1_CACHE_BYTES - 2 * sizeof(int)];

	unsigned		toIndex(unsigned i){return i & (ringSize - 1);}
	
	Ring(unsigned int size):ringSize(size), next(NULL), pi(0), ci_snapshot(0), 
							ci(0), pi_snapshot(0){
		array = new CruiserNode[size];
		assert(array);
	}
	
	~Ring(){delete [] array;}
	
	unsigned getSize(){return ringSize;}
	
	bool	produce(const CruiserNode & node){
#ifdef CRUISER_DEBUG
		fprintf(stderr, "produce: This thread id %lu, user addr %p, ring %p, \
				ringSize %u, ci %u, pi %u\n", (unsigned long)(pthread_self()), 
				node.userAddr, this, ringSize, ci, pi);
#endif
		ASSERT(node.userAddr);
		if((pi -ci_snapshot) >= ringSize){
			if((pi -ci) >= ringSize)
				return false;
			ci_snapshot = ci;
		}
		array[toIndex(pi)] = node; // Element value assignment
		//Make sure the consumer sees the update of the element vaule before pi
		//__sync_synchronize(); // note needed in x86?
		pi++;
		return true;
	}
	
	bool	consume(CruiserNode & node){
		if(ci == pi_snapshot){
			if( ci == pi)
				return false;
			pi_snapshot = pi;
		}
			
		node = array[toIndex(ci)];
		//__sync_synchronize();
		ci++;
		return true;
	}	
};

/* A traditional ring implementation.
class Ring{
private:	
	CruiserNode		*array;
	unsigned 		ringSize;
	//Using padding to avoid false sharing between different processors. 
	//char			pad1[128 - sizeof(int*) * 2];
	unsigned 		volatile pi;//producer index
	//char			pad2[128 - sizeof(int)];
	unsigned 		volatile ci;//consumer index
	//char			pad3[128 - sizeof(int)];
	
	bool			isFull(){return (pi -ci) >= ringSize;}
	bool			isEmpty(){return ci == pi;}
	unsigned		toIndex(unsigned i){return i & (ringSize - 1);}
	
public:
	Ring			*next;
	Ring(int size):next(NULL), ringSize(size), pi(0), ci(0){
		array = new CruiserNode[size];
		assert(array);
	}
	
	~Ring(){delete [] array;}
	
	unsigned getSize(){return ringSize;}
	
	bool	produce(const CruiserNode & node){
		ASSERT(node.userAddr);
		if(isFull()){
			return false;
		}
		array[toIndex(pi)] = node;
		__sync_synchronize();
		pi++;
		return true;
	}
	
	bool	consume(CruiserNode & node){
		if(isEmpty())
			return false;
		node = array[toIndex(ci)];
		__sync_synchronize();
		ci++;
		return true;
	}	
};
*/

class ThreadRecord{
public:
	// The updates of pr and cr are rare, so false sharing is acceptable
	Ring			*pr; // The ring currently accessed by the producer
#ifdef EXP // for accounting
	unsigned		pCount; // The number of produced nodes.
	unsigned		pDropped; // The number of dropped nodes.
	char			cache_pad0[L1_CACHE_BYTES - 3 * sizeof(int)];
	unsigned		cCount; // The number of consumed nodes.
#endif
	Ring			*cr; // The ring currently accessed by the consumer

	ThreadRecord	* volatile next; // To form a list of threadRecords
	pthread_t	volatile threadID; // threadID = 0 means it is available.
	ThreadRecord(unsigned int initialSize = RING_SIZE){
#ifdef EXP
		pCount = pDropped = cCount = 0;
#endif
		threadID = pthread_self();
		Ring* p = new Ring(initialSize);
		assert(p);
		pr = cr = p;
	}

#ifdef EXP
	void resetCount(){
		pCount = pDropped = cCount = 0;	
	}
#endif

	// Invoked by the user thread.
	bool	produce(const CruiserNode & node){
//#endif

#ifdef EXP
		pCount++;
#endif
		if( pr->produce(node) ){
			return true;
		}
		unsigned newSize = pr->getSize() * 2;
		if(newSize > MAX_RING_SIZE)
			newSize = MAX_RING_SIZE;
		// We are now in the user thread, so allocate using the original malloc 
		// in order to avoid infinite recursions.
		t_protect = 0;
		Ring	*pNew = new Ring(newSize);
		t_protect = 1;
		if(pNew){
			pNew->produce(node);
			// The two lines need testing about the writing order.
			pr->next	= pNew;
			pr			= pNew;
			return true;
		}
#ifdef EXP
		pDropped++;
#endif
		return false;

	}
	
	// Invoked by the transmitter thread.
	bool	consume(CruiserNode & node){
		if( cr->consume(node) ){
#ifdef EXP
			cCount++;
#endif
			return true;
		}
		if(cr->next){
			Ring *pOld = cr;
			cr = cr->next;
			delete pOld;
#ifdef EXP
			cCount++;//assume the line below is successful.
#endif
			return cr->consume(node);
		}
		return false;
	}
};

class ThreadRecordList{
public:
	ThreadRecord * volatile head;
	
	ThreadRecordList():head(NULL){}

#ifdef EXP
	void resetCount(){
		for(ThreadRecord *p = head; p != NULL; p = p->next){
			p->resetCount();
		}	
	}
#endif
	
	ThreadRecord* getThreadRecord(){
#ifdef CRUISER_DEBUG
	fprintf(stderr, "thread %lu is in getThreadRecord, t_protect= %d\n", 
			(unsigned long)(pthread_self()), t_protect);
#endif
		pthread_t self = pthread_self();
		ThreadRecord *p;
		for(p = head; p != NULL; p = p->next){
			if(p->threadID == 0 && 
					__sync_bool_compare_and_swap(&p->threadID, NULL, self))
				return p;
		}
		// TODO: use a sandwich structure to protect cruiser data.
		t_protect = 0;
		p = new ThreadRecord();
		t_protect = 1;
		assert(p);
		ThreadRecord *oldHead;
		do{
			oldHead = head;
			p->next = oldHead;
		}while(!__sync_bool_compare_and_swap(&head, oldHead, p));
		return p;
	}
};

static ThreadRecordList * g_threadrecordlist;

static __thread ThreadRecord	*t_threadRecord  = NULL;

}//namespace cruiser

#endif //THREAD_RECORD_H
