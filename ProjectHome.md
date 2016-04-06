We deliver a concurrent heap buffer overflow detector, Cruiser, in which a concurrent thread is added to the user program to monitor heap integrity. Custom lock-free data structures and algorithms are designed to achieve high efficiency and scalability. The experiments show that our approach is practical: it imposes an average of 5% performance overhead on SPEC CPU2006, and the throughput slowdown on Apache is negligible on average.

Cruiser can detect buffer overflows that occur in any function, rather than specific libc functions. It can detect buffer underflows, duplicate frees, and memory leakage. Cruiser does not rely on specific memory allocation algorithms; it can work with any memory allocator.

Cruiser is legacy code compatible and can be applied to protect binary executables transparently, thus no source code or binary rewriting is needed. It is implemented as a shared library. Hence, Cruiser can be deployed easily to production systems such as data centers and server farms in an automated manner.

More details please refer to our paper:

Qiang Zeng, Dinghao Wu, Peng Liu, Cruiser: concurrent heap buffer overflow monitoring using lock-free data structures, Proceedings of the 32nd ACM SIGPLAN conference on Programming language design and implementation, p367-377, June 04-08, 2011, San Jose, California, USA