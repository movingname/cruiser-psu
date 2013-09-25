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

#ifndef LIST_H
#define LIST_H

#include "common.h"

namespace cruiser{
// 4M
#define 		LIST_RING_SIZE 0x400000U 
// 4 * 64 / 4 or 8 = 64 (32bit system) or 32 (64bit system)
#define			BATCH_SIZE	(4 * L1_CACHE_BYTES / sizeof(int*)) 

// Note: this is the ring used for caching CruiserNodes; it is NOT the 
// cruiserRing for transmitting buffer addresses.
template<typename T, unsigned int ringSize> 
class RingT{
private:
// The cacheline protection is improved over the Hong Kong paper:
// pi and ci are in different cachelines (while in the hongkong paper, 
// they are in the same cacheline). So the producer owns the cacheline 
// exclusive most of the time, except for "pi" is read by the consumer 
// occassionally.
	char				cache_pad0[L1_CACHE_BYTES];
	T* 				array[ringSize];
	unsigned int	volatile 	pi; // The producer index; read by the consumer
	char 				cache_pad1[L1_CACHE_BYTES-sizeof(int)];
	unsigned int	volatile 	ci; // The consumer index; read by the producer
	char 				cache_pad2[L1_CACHE_BYTES-sizeof(int)];
	// The consumer's local variables
	unsigned int 			pi_snapshot;
	unsigned int 			ci_current; // ci = ci_current, per batch
	unsigned int 			ci_batch;
	char				cache_pad3[L1_CACHE_BYTES-3*sizeof(int)];
	// The producer's local variables
	unsigned int 			ci_snapshot;
	unsigned int 			pi_current; // pi = pi_current, per batch
	unsigned int 			pi_batch;
	char				cache_pad4[L1_CACHE_BYTES-3*sizeof(int)];

	//bool			isFull(){return (pi -ci >= ringSize);}
	//bool			isEmpty(){return ci == pi;}
	unsigned		toIndex(unsigned i){return i & (ringSize - 1);}
	
public:
	RingT(unsigned preFilled){
		assert(preFilled < ringSize);
		for(unsigned int i = 0; i < preFilled; i++){
			array[i] = (T*) original_malloc(sizeof(T));
		}
		pi = pi_current = pi_snapshot = preFilled;
		ci = ci_current = ci_snapshot = ci_batch = pi_batch = 0;
	}
	
	bool produce(T *node){
		if(pi_current - ci_snapshot >= ringSize){
			if(pi_current - ci >= ringSize)
				return false;
			else
				ci_snapshot = ci;
		}
		array[toIndex(pi_current)] = node; // Entry value assignment
		pi_current++;
		pi_batch++;
		if(pi_batch >= BATCH_SIZE){
			pi_batch = 0;
			// Make sure the consumer sees the entry vaule assignment 
			// before it sees pi is updated. 
			// __sync_synchronize(); // not needed in x86?
			pi = pi_current;
		}
		return true;
	}
	
	bool consume(T * &node){
		if(ci_current == pi_snapshot){
			if(ci_current == pi)
				return false;
			else
				pi_snapshot = pi;
		}
		node = array[toIndex(ci_current)];
		ci_current++;
		ci_batch++;
		if(ci_batch >= BATCH_SIZE){
			ci_batch = 0;
			//__sync_synchronize();
			ci = ci_current;
		}
		return true;
	}
};




#ifdef CRUISER_OLD_LIST
// Below is a less efficient list design, which uses Compare-And-Swap to
// insert nodes.
class List:public NodeContainer{
private:
	class ListNode{
	public:
		CruiserNode	cn;
		ListNode	*next;
	};
	
	RingT<ListNode, LIST_RING_SIZE>	ring;
	
	// Below is an incorrect design by using an array as the pre-allocated 
	// storage, because if a ListNode can not be inserted back into the 
	// ring, it has to be freed, which is not allowed for array elements.
	// ListNode 		nodeArray[LIST_RING_SIZE];
	
	ListNode 		dummy;
	
public:	
	#define PRE_ALLOCATED_FACTION 0.25

	List():ring(PRE_ALLOCATED_FACTION * LIST_RING_SIZE){
		dummy.cn.userAddr = NULL;
		dummy.next = NULL;
	}
	//pushFront
	bool insert(const CruiserNode & node){
		ListNode* pn;
		if(!ring.consume(pn))
			pn = (ListNode*)original_malloc( sizeof(ListNode) );
		assert(pn);
		pn->cn = node;
		do{
			pn->next = dummy.next;
		}while(!__sync_bool_compare_and_swap(&dummy.next, pn->next, pn));
		return true;
	}
	
	int traverse( int (*pfn)(const CruiserNode &) );
};

int List::traverse( int (*pfn)(const CruiserNode &) ){
		ListNode *cur, *prev, *next;	
		bool bFirst;
again:	
			prev = &dummy;
			cur = dummy.next;
			bFirst = true;
		while(true){
			//if(g_stop)
			//	return 0;
			if(!cur)
				return 1;
			// pfn Return values:
			// 	0: to stop monitoring (obosolete)
			//	1: have checked one node
			//	2: have encountered a dummy node (should never happen)
			//	3: a node to be removed
			switch(pfn(cur->cn)){
				//case 0:
				//	return 0;
				case 1:
					prev = cur;
					cur = cur->next;
					bFirst = false;
					break;
				// Should never happen because currently 
				// there is only one list segement.
				//case 2:
				//	return 2;
				case 3:
					next = cur->next;
					if( bFirst ){
						if( __sync_bool_compare_and_swap(&dummy.next, cur, next) ){
							if(!ring.produce(cur))
								original_free(cur);
						}
						// No matter the deletion succeeded or not, traverse 
						// again. Otherwise, the "prev" variable may not point to 
						// the previous node of the node pointed to by the 
						// "next" variable.
						goto again;
					}else{
						assert(prev->next == cur);
						prev->next = next;
						if(!ring.produce(cur))
							original_free(cur);
						cur = next;
					}
					break;
	
			}
		
		}
}

#else //ifndef CRUISER_OLD_LIST

// Below is the list as described in the paper.
// It uses a ring to cache the deleted list nodes in order to reuse them later.
class List:public NodeContainer{
private:
	class ListNode{
	public:
		CruiserNode	cn;
		ListNode	*next;
		void markDelete(){cn.userAddr = (void*)-1L;}
		bool isMarkedDelete(){return cn.userAddr == (void*)-1L;}
	};
	
	RingT<ListNode, LIST_RING_SIZE>	ring;
	
	ListNode 		dummy; 
	
public:	
	#define PRE_ALLOCATED_FACTION 0
	
	List():ring(PRE_ALLOCATED_FACTION * LIST_RING_SIZE){
		dummy.next = NULL;
		dummy.cn.userAddr = NULL;
	}
	//pushFront
	bool insert(const CruiserNode & node){
		ListNode* pn;
		if(!ring.consume(pn))
			pn = (ListNode*)original_malloc( sizeof(ListNode) );
		assert(pn);
		pn->cn = node;
		pn->next = dummy.next;
		dummy.next = pn;
		return true;
	}
	
	int traverse( int (*pfn)(const CruiserNode &) );
};

int List::traverse( int (*pfn)(const CruiserNode &) ){
	ListNode *prev, *cur, *next;
	cur = dummy.next;
	if(!cur)
		return 1;
	if(!cur->isMarkedDelete()){
		// pfn Return values:
		// 	0: to stop monitoring (obsolete);
		//	1: have checked one node
		//	2: have encountered a dummy node (should never happen)
		//	3: a node is to be removed
		if(pfn(cur->cn) == 3)
			cur->markDelete();
	}
	
	prev = cur;
	cur = cur->next;
	while(NULL != cur){
		next = cur->next;
		if(cur->isMarkedDelete()){
			prev->next = next;
			if(!ring.produce(cur))
				original_free(cur);//delete cur;	
		}else{
			switch(pfn(cur->cn)){
				// As the "stop monitoring" feature may be exploited,
				// we disallow it in this implementation.
				//case 0: // Stop monitoring
				//	return 0;
				case 1:
					prev = cur;
					break;
				// Should never happen because for this implementation
				// there is only one dummy node.
				//case 2:
				//	return 2;
				case 3:
					prev->next = next;
					if(!ring.produce(cur))
						original_free(cur);
					break;
				default:
					break;
			}
		}
		cur = next;
	}
	return 1;
}
#endif //CRUISER_OLD_LIST

}//namespace cruiser
#endif //LIST_H
