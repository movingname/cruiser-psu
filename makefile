CC		= g++ 
# "-fPIC -shared" is to create a shared lib.
# "-march=native" means the compiler optimizes according to the host processsor.
# "-march=xxx is also necessary for using __sync_compare_and_swap.
CFLAGS	= -Wall -O2 -shared -fPIC -march=native 
LDFLAGS	= -ldl -pthread # "-ldl" is for dlsym().
SRC		= memory.cpp

# L: lazy-cruiser; buffers are deallocated by cruiser; -DDELAYED is used.
# E: eager-cruiser; buffers are deallocated by user threads.
# X: experiment; print statistics before exit; -DEXP is used.
# S: single-threaded program; print more statistics than X before exit, 
#    e.g., the number of allocations; -DSINGLE_EXP is used.
# D: debug; print verbose information during execution; -DCRUISER_DEBUG is used.
# -DAPACHE: we have special code to deal with the fork() in Apache and make
#				cruiser go to sleep for the inactive backup apache process.
# -DSPEC: we have special code to ignore the assistant process in SPEC.
#
# Other macros: 
#	-DMONITOR_EXIT: delay the process for at most 1 second to give the monitor
#				some time to finish the last round of buffer checking.
#	-DCHECK_DUPLICATE_FREES: enable the checking duplicate frees.
#	-DNMONITOR: the allocation is hooke and the buffer is encapsulated; but 
#				the monitor and deliver threads are not created.
#	-DNPROTECT: the allocation is simply passed to the original functions.
#
# Other controls: 
#	The user can set up the two environment variables to reduce the overhead.
#	E.g., CRUISER_SLEEP=1 CRUISER_NOP=50 
#	The reason why they are not MACROS is to avoid recompilation when you change
#	the value:
# 		CRUISER_SLEEP: the number of milliseconds the monitor thread will sleep
#						for after each round of heap check.
# 		CRUISER_NOP: the number of NOP operations the monitor thread will issue
#						after checking one buffer.

all: lazy-cruiser eager-cruiser lazy-cruiser-extra eager-cruiser-extra test

lazy-cruiser: L 

eager-cruiser: E

lazy-cruiser-extra: LX LSX LD LX-Spec L-Apache

eager-cruiser-extra: EX LSX ED EX-Spec E-Apache

L:
	# -DNDEBUG: to disable all assertions (pls refer to assert.h).
	$(CC) $(CFLAGS) -DNDEBUG -DDELAYED -o liblazycruiser.so $(SRC) $(LDFLAGS)

E:
	$(CC) $(CFLAGS) -DNDEBUG -o libeagercruiser.so $(SRC) $(LDFLAGS)

# lazy-cruiser-extra
LX:
	$(CC) $(CFLAGS) -DDELAYED -DEXP -o liblazyexpcruiser.so $(SRC) $(LDFLAGS)

LSX:
	$(CC) $(CFLAGS) -DDELAYED -DEXP -DSINGLE_EXP -o liblazysingleexpcruiser.so $(SRC) $(LDFLAGS)

LD:
	$(CC) $(CFLAGS) -DDELAYED -DCRUISER_DEBUG -o liblazydebugcruiser.so $(SRC) $(LDFLAGS)

LX-Spec:
	$(CC) $(CFLAGS) -DDELAYED -DEXP -DSPEC -o liblazyexpcruiser-spec.so $(SRC) $(LDFLAGS)

L-Apache:
	$(CC) $(CFLAGS) -DDELAYED -DAPACHE -o liblazycruiser-apache.so $(SRC) $(LDFLAGS) 

# eager-cruiser-extra
EX:
	$(CC) $(CFLAGS) -DEXP -o libeagerexpcruiser.so $(SRC) $(LDFLAGS)

ESX:
	$(CC) $(CFLAGS) -DEXP -DSINGLE_EXP -o libeagersingleexpcruiser.so $(SRC) $(LDFLAGS)

ED:
	$(CC) $(CFLAGS) -DCRUISER_DEBUG -o libeagerdebugcruiser.so $(SRC) $(LDFLAGS)

EX-Spec:
	$(CC) $(CFLAGS) -DEXP -DSPEC -o libeagerexpcruiser-spec.so $(SRC) $(LDFLAGS)

E-Apache:
	$(CC) $(CFLAGS) -DAPACHE -o libeagercruiser-apache.so $(SRC) $(LDFLAGS)

# $(SRC): utility.h common.h list.h monitor.h thread_record.h

# simpleTest is a simple multi-threaded program allocating/deallocating buffers.
# effectTest contains some heap errors, like overflows, duplicate-frees.
# usage: LD_PRELOAD=./lib*cruiser.so simple.out
#		 LD_PRELOAD=./lib*cruiser.so effectTest.out
test:
	$(CC) -Wall -o simpleTest.out simpleTest.cpp -pthread
	$(CC) -Wall -o effectTest.out effectTest.cpp -ldl

clean:
	rm *.o *.so *.out 
