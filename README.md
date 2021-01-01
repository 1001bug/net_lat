# Measure network tatancy
For 10Gb+ LAN on Linux and Windows (+Cygwin)
- unicast send cost
- multicast send const
- ping-pong with 8 byte payload different ways
- time diffirence between two servers
- max thouput
- TCP and UDP

# What to expect
User-space -> kernel -> card about 5usec
Good CISCO 10G ETHERNET VLAN switch from card port to card port about 5-6usec
card -> Kernel -> user-space on other side same 5usec
So good UDP ping-pong is about 30usec
Using network-traffic offload libs like Mellanox VMA for low letancy can cut not so match. Affects "User-space->kernel->card" path only. So 5 usec can become 1-2usec on each side where offload is used.  

+ Tested on RHel Linux 6&7, Windows (Cygwin). 
+ Windows network stack not so bad like most whould expect. On same hardware difference is about 5-10%. Test not for inernet connections. 
+ Is is about 10G, 40G LAN. And all is about low letency trade systems. 
+ Well-tuned TCP loike Linux vs Windows - 10% slower. 
+ Multicat syscall slower then unicast. 600K syscall to unicast vs 400K to multiocast on 3-4MHz Intel CPU and 10G Intel card
+ RHel7 RT kernel multicast dramaticly slower then nonRT kernel. (100% it is not about RHel, but did not tested elswhere)
+ multiinstance UDP send can do 1M+ syscalls

# Compille
Is is Netbians C/C++ project but one file project. So can be compilled inplace: gcc -std=c99 -lrt main.c -o net_lat
Code not idial. Look inside to find more answers

