CC		= g++ 
# "-fPIC -shared" is to create a shared lib.
# "-march=native" means the compiler optimizes according to the host processsor.
# "-march=xxx is also necessary for using __sync_compare_and_swap.
CFLAGS	= -Wall -O2 -shared -fPIC -march=native 
LDFLAGS	= -ldl -lpthread # "-ldl" is for dlsym().
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

L: $(SRC) 
	# -DNDEBUG: to disable all assertions (pls refer to assert.h).
	$(CC) $(CFLAGS) $(LDFLAGS) -DNDEBUG -DDELAYED -o liblazycruiser.so $(SRC)

E: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DNDEBUG -o libeagercruiser.so $(SRC)

# lazy-cruiser-extra
LX: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DDELAYED -DEXP -o liblazyexpcruiser.so $(SRC)

LSX: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DDELAYED -DSINGLE_EXP -o liblazysingleexpcruiser.so $(SRC)

LD: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DDELAYED -DCRUISER_DEBUG -o liblazydebugcruiser.so $(SRC)

LX-Spec: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DDELAYED -DEXP -DSPEC -o liblazyexpcruiser-spec.so $(SRC)

L-Apache: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DDELAYED -DAPACHE -o liblazycruiser-apache.so $(SRC)

# eager-cruiser-extra
EX: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DEXP -o libeagerexpcruiser.so $(SRC)

ESX: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DSINGLE_EXP -o libeagersingleexpcruiser.so $(SRC)

ED: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DCRUISER_DEBUG -o libeagerdebugcruiser.so $(SRC)

EX-Spec: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DEXP -DSPEC -o libeagerexpcruiser-spec.so $(SRC)

E-Apache: $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -DAPACHE -o libeagercruiser-apache.so $(SRC)

$(SRC): utility.h common.h list.h monitor.h thread_record.h

# simpleTest is a simple multi-threaded program allocating/deallocating buffers.
# effectTest contains some heap errors, like overflows, duplicate-frees.
# usage: LD_PRELOAD=./lib*cruiser.so simple.out
#		 LD_PRELOAD=./lib*cruiser.so effectTest.out
test: simpleTest.cpp effectTest.cpp
	$(CC) -Wall -lpthread -o simpleTest.out simpleTest.cpp
	$(CC) -Wall -ldl -o effectTest.out effectTest.cpp

clean:
	rm *.o *.so *.out 
