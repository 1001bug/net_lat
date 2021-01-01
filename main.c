/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   main.c
 * Author: ermolovag
 *
 * Created on 26 января 2018 г., 17:38
 */


/*
build with
gcc -std=c99 -lrt net_lat.c -o net_lat
 */

#define _GNU_SOURCE

#include <sched.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>

#include <getopt.h>
#include <inttypes.h>

#ifndef TCP_QUICKACK
#define TCP_QUICKACK     12     /* quick ACKs.  */
#endif

//сделать переменный размер, но время писать в начало
#define LOAD_SIZE 8 /* 8 byte*/
#define PORT "11000"
#define USEC 100000
#define DIF_ARR_SIZE 200


//без этого не запускается print_stat тред
#define THREADSTACK  65536

static volatile int keepRunning = 1;
static volatile int hardBrake = 0;

static volatile long global_count_snd = 0;
static volatile long global_count_rcv = 0;


int BYTES_LOAD = LOAD_SIZE;
int SOKET_BUFFER_SIZE = 4194304; //4mb
int mmsghdr_size = 10; //size of struct mmsghdr for mass socket read - recvmmsg

void* print_stat_every_sec_thread(void *OL);
int long_cmp(const void *a, const void *b);
void intHandler(int dummy);
long getMicrotime();

int tcp_server(struct addrinfo *res_local);
int udp_server(struct addrinfo *res_local, int push_local_time);

int tcp_client(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs);
int udp_client(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs, int get_remote_time);
int gess_cpu_cores(int *core1, int *core2);

union time_usec_buf {
    unsigned char char_buf[ LOAD_SIZE ];
    int64_t usec;
};

//надо сделать возможном отправлять пакеты разного размера
//temp!!!
#define BUF_ITEMS 1000
#define BUF_ITEM_SIZE LOAD_SIZE

typedef struct thread_start_params {
    int socket;
    int cpu_num;
} thread_start_params;

typedef struct FIFO {
    struct sockaddr remote_client_addr;
    socklen_t addr_size;
    int buf_items;
    int buf_item_size;
    //unsigned char *head;
    //unsigned char *tail;
    int head;
    int tail;
    unsigned char **buf;
} FIFO;


struct FIFO QUEUE;






//void* udp_server_max_fifo_snd(void *r_l) {
//void* udp_server_max_fifo_snd(void *sk) {

void* udp_server_max_fifo_snd(void *param) {


    
    //struct addrinfo *res_local=(struct addrinfo *)r_l;

    //int sock = *(int *) sk;
    struct thread_start_params P = *(struct thread_start_params*) param;
    int sock = P.socket;

#ifndef __CYGWIN__    
    int cpu = P.cpu_num;
    if (cpu) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof (cpu_set_t), &set) == -1)
            perror("pthread_setaffinity_np fifo_snd");
    }
#endif
    fprintf(stderr, "snd thread cpu %i\n", sched_getcpu());
    while (keepRunning) {
        if (QUEUE.tail == QUEUE.head) {
            //empty
            //NOP????
            continue;
        }

        int next_e = QUEUE.tail + 1;
        if (next_e == QUEUE.buf_items)
            QUEUE.tail = 0;
        else
            QUEUE.tail = next_e;


        while (keepRunning) {
            errno = 0;
            int k = sendto(sock, QUEUE.buf[QUEUE.tail], LOAD_SIZE, 0, &QUEUE.remote_client_addr, QUEUE.addr_size);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto_fifo_snd");
                    struct sockaddr_in *addr_in = (struct sockaddr_in *) (&QUEUE.remote_client_addr);
                    char *s = inet_ntoa(addr_in->sin_addr);
                    printf("sock %i, QUEUE.tail %i, LOAD_SIZE %i, destination ip %s\nExit sending thread\n", sock, QUEUE.tail, LOAD_SIZE, s);

                    //EXIT THREAD
                    return NULL;
                }
            } else {
                break;
            }
        }

    }//while


    return NULL;


}

int gess_cpu_cores(int *core1, int *core2) {
#ifndef __CYGWIN__
    cpu_set_t cur_cpu_set;
    CPU_ZERO(&cur_cpu_set);

    if (sched_getaffinity(getpid(), sizeof (cur_cpu_set), &cur_cpu_set) == -1)
        perror("sched_setaffinity");
    //int rcv_cpu = 0;
    //int snd_cpu = 0;

    //идем с конца
    if (CPU_COUNT(&cur_cpu_set) > 1) {
        for (int i = CPU_SETSIZE; i >= 0; i -= 1) {
            int isset = CPU_ISSET(i, &cur_cpu_set);
            //printf("%i ", isset);
            if (isset) {
                if (*core1 == 0)
                    *core1 = i;
                else if (*core2 == 0)
                    *core2 = i;
                else
                    break;
            }
            //printf("\n");
        }
    } else
        fprintf(stderr, "make assign 2 cpu with taskset for better perfomance (Curent affinity mask contains %i cpu)\n", CPU_COUNT(&cur_cpu_set));
#endif
    return (*core1 > 0) && (*core2 > 0);
}

int udp_server_max_fifo_rcv(struct addrinfo *res_local) {



    //printf("\nCout CUP %i\n",CPU_COUNT(&set));
    //printf("\nCur CUP %i\n",cpu);
    struct thread_start_params P = {.socket = 0, .cpu_num = 0};


    int sock;
    //    int bytes_read;
    //    socklen_t addr_size;
    //union time_usec_buf u_buf;


    //struct sockaddr remote_client_addr;
    //socklen_t addr_size;

    //QUEUE.remote_client_addr=&remote_client_addr;
    QUEUE.addr_size = sizeof (struct sockaddr);

    //addr_size = sizeof remote_client_addr;

    QUEUE.buf = calloc(BUF_ITEMS, sizeof (unsigned char *));
    for (int i = 0; i < BUF_ITEMS; i += 1) {
        QUEUE.buf[i] = calloc(BUF_ITEM_SIZE, sizeof (unsigned char));
    }
    //QUEUE.head=QUEUE.tail=QUEUE.buf[0];

    QUEUE.head = QUEUE.tail = 0;
    QUEUE.buf_items = BUF_ITEMS;
    QUEUE.buf_item_size = BUF_ITEM_SIZE;


    sock = socket(res_local->ai_family, res_local->ai_socktype, res_local->ai_protocol);

    P.socket = sock;

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind");
        exit(2);
    }
    fcntl(sock, F_SETFL, O_NONBLOCK);




    //очередь проинитили, стартуем стред отправки

    pthread_t SND_pth;
    pthread_attr_t pth_attrs_snd;

    pthread_attr_init(&pth_attrs_snd);
    pthread_attr_setstacksize(&pth_attrs_snd, THREADSTACK);



#ifndef __CYGWIN__
    {//CPU

        cpu_set_t snd_set, rcv_set;
        CPU_ZERO(&snd_set);
        CPU_ZERO(&rcv_set);

        int rcv_cpu = 0;
        int snd_cpu = 0;

        if (gess_cpu_cores(&rcv_cpu, &snd_cpu)) //return 1 on success and 0 on fail
        {
            fprintf(stderr, "rcv_cpu %i, snd_cpu %i\n", rcv_cpu, snd_cpu);

            //если удалось что-то назначить
            //if (rcv_cpu != 0 && snd_cpu != 0) {//лишняя проверка
            //fprintf(stderr,"rcv_cpu %i, snd_cpu %i\n",rcv_cpu,snd_cpu);

            CPU_SET(snd_cpu, &snd_set);
            CPU_SET(rcv_cpu, &rcv_set);


            //pthread_attr_setaffinity_np(&pth_attrs_snd, sizeof (cpu_set_t), &snd_set);


            //if (sched_setaffinity(getpid(), sizeof (cpu_set_t), &rcv_set) == -1)
            //perror("rcv sched_setaffinity");
            if (pthread_setaffinity_np(pthread_self(), sizeof (cpu_set_t), &rcv_set) == -1)
                perror("pthread_setaffinity_np fifo_rcv");

            //}
        } else
            fprintf(stderr, "Cannot gess cpu affinity, rcv_cpu %i, snd_cpu %i\n", rcv_cpu, snd_cpu);


        P.cpu_num = snd_cpu;


    }//CPU
#endif    
    //if (pthread_create(&SND_pth, &pth_attrs_snd, udp_server_max_fifo_snd, res_local) == -1) {
    if (pthread_create(&SND_pth, &pth_attrs_snd, udp_server_max_fifo_snd, &P) == -1) {
        perror("print_stat_every_sec_thread create");
        exit(EXIT_FAILURE);
    }
    pthread_setname_np(SND_pth, "sender");

    //*/


    //    unsigned char buf[BUF_ITEM_SIZE]={0};

    fprintf(stderr, "rcv thread cpu %i\n", sched_getcpu());
    while (keepRunning) {//бесконечно
        int next_e = QUEUE.head + 1;
        if (next_e == QUEUE.buf_items)
            next_e = 0;
        while (next_e == QUEUE.tail) {//ждем свободной ячейки
            //stall
            fprintf(stderr, "QUEUE in stall! sleep 1 usec\n");
            usleep(1);
        }
        while (keepRunning) {//читаем пока не получим сообщение
            errno = 0;
            int k = recvfrom(sock, QUEUE.buf[next_e], BUF_ITEM_SIZE, 0, &QUEUE.remote_client_addr, &QUEUE.addr_size);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    //printf("errno %i\n",errno);
                    continue;
                } else {
                    perror("recvfrom_fifo_rcv");
                    return -1;
                }
            }
            break;
        }
        //printf("recv one\n");
        //struct sockaddr_in *addr_in = (struct sockaddr_in *)(&QUEUE.remote_client_addr);
        //char *s = inet_ntoa(addr_in->sin_addr);
        //printf("RCV IP address: %s\n", s);
        //успешно вычитав, передвигаемся вперед
        QUEUE.head = next_e;

    }

    return 0;


}

void* udp_client_max_async_rcv(void *param) {

    struct thread_start_params P = *(struct thread_start_params*) param;
    int sock = P.socket;


#ifndef __CYGWIN__    
    int cpu = P.cpu_num;
    if (cpu) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (pthread_setaffinity_np(pthread_self(), sizeof (cpu_set_t), &set) == -1)
            perror("pthread_setaffinity_np");
    }
#endif

    unsigned char *buf = calloc(LOAD_SIZE, sizeof (unsigned char));




    fprintf(stderr, "rcv thread cpu %i\n", sched_getcpu());


    while (keepRunning) {
        errno = 0;
        int k = recvfrom(sock, buf, LOAD_SIZE, 0, NULL, NULL);
        if (k < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            } else {
                perror("EXIT THREAD on recvfrom()");
                return NULL;
            }
        } else {

            global_count_rcv += 1;
        }
    }//while
    return NULL;
}

int udp_client_max_async_snd(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs) {
    int sock;
    unsigned char *buf = calloc(LOAD_SIZE, sizeof (unsigned char));
    struct thread_start_params P = {.socket = 0, .cpu_num = 0};


    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    P.socket = sock;

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind local addr");
        exit(2);
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);





    pthread_t PS_pth;
    pthread_attr_t pth_attrs_ps;
    pthread_attr_init(&pth_attrs_ps);
    pthread_attr_setstacksize(&pth_attrs_ps, THREADSTACK);

    if (pthread_create(&PS_pth, &pth_attrs_ps, print_stat_every_sec_thread, NULL) == -1) {
        perror("print_stat_every_sec_thread create");
        exit(EXIT_FAILURE);
    }
    pthread_setname_np(PS_pth, "print_stat");



    pthread_t RCV_pth;
    pthread_attr_t pth_attrs_rcv;

    pthread_attr_init(&pth_attrs_rcv);
    pthread_attr_setstacksize(&pth_attrs_rcv, THREADSTACK);

#ifndef __CYGWIN__
    {//CPU

        cpu_set_t snd_set, rcv_set;
        CPU_ZERO(&snd_set);
        CPU_ZERO(&rcv_set);

        int rcv_cpu = 0;
        int snd_cpu = 0;

        if (gess_cpu_cores(&rcv_cpu, &snd_cpu)) {
            CPU_SET(snd_cpu, &snd_set);
            CPU_SET(rcv_cpu, &rcv_set);

            if (pthread_setaffinity_np(pthread_self(), sizeof (cpu_set_t), &snd_set) == -1)
                perror("pthread_setaffinity_np snd_set");
        } else
            fprintf(stderr, "Cannot gess cpu affinity, rcv_cpu %i, snd_cpu %i\n", rcv_cpu, snd_cpu);




        fprintf(stderr, "rcv_cpu %i, snd_cpu %i\n", rcv_cpu, snd_cpu);
        P.cpu_num = rcv_cpu;
    }//CPU
#endif


    //if (pthread_create(&SND_pth, &pth_attrs_snd, udp_server_max_fifo_snd, res_local) == -1) {
    if (pthread_create(&RCV_pth, &pth_attrs_rcv, udp_client_max_async_rcv, &P) == -1) {
        perror("print_stat_every_sec_thread create");
        exit(EXIT_FAILURE);
    }
    pthread_setname_np(RCV_pth, "reciver");





    fprintf(stderr, "snd thread cpu %i\n", sched_getcpu());
    while (keepRunning) {



        while (keepRunning) {
            errno = 0;
            int k = sendto(sock, buf, LOAD_SIZE, 0, res_remote->ai_addr, res_remote->ai_addrlen);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto");
                    return -1;
                }
            } else {

                global_count_snd += 1;
                if (usecs > 0)
                    usleep(usecs);

            }
        }

    }
    return 0;
}

int udp_client(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs, int get_remote_time) {
    int sock;
    union time_usec_buf u_buf;
    struct timeval local_time;

    //    int bytes_read;
    memset(&u_buf, 0, LOAD_SIZE);

    int n = 0;


    long dif[DIF_ARR_SIZE] = {0};
    long dif_sorted[DIF_ARR_SIZE] = {0};
    //memset(dif,0,DIF_ARR_SIZE*sizeof(long));
    //memset(dif_sorted,0,DIF_ARR_SIZE*sizeof(long));
    /*for (int i = 0; i < 100; i++) {
        dif[i] = 0;
        dif_sorted[i] = 0;
    }*/


    struct timespec start, end;

    setbuf(stdout, NULL);

    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind local addr");
        exit(2);
    }



    fcntl(sock, F_SETFL, O_NONBLOCK);
    while (keepRunning) {

        clock_gettime(CLOCK_MONOTONIC, &start);
        while (keepRunning) {
            errno = 0;
            int k = sendto(sock, &u_buf, LOAD_SIZE, 0, res_remote->ai_addr, res_remote->ai_addrlen);

            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto");
                    return -1;
                }
            } else
                break;
        }


        while (keepRunning) {
            errno = 0;
            int k = recvfrom(sock, &u_buf, LOAD_SIZE, 0, NULL, NULL);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("recvfrom");
                    return -1;
                }
            } else {
                clock_gettime(CLOCK_MONOTONIC, &end);
                break;
            }
        }
        if (get_remote_time) {
            gettimeofday(&local_time, NULL);
            u_buf.usec -= local_time.tv_sec * (long) 1e6 + local_time.tv_usec;
        }


        dif[n] = ((end.tv_sec * (long) 1e9 + end.tv_nsec)-(start.tv_sec * (long) 1e9 + start.tv_nsec)) / 1000;




        //for (int i = 0; i < 100; i++)dif_sorted[i] = dif[i];
        memcpy(dif_sorted, dif, DIF_ARR_SIZE);
        qsort(dif_sorted, DIF_ARR_SIZE, sizeof (long), long_cmp);
        printf("Last %4li usecs (MEAN %4li usecs)%s", dif[n], dif_sorted[0] > 0 ? dif_sorted[(int) (DIF_ARR_SIZE / 2)] : -1, get_remote_time ? "" : "\n");
        if (get_remote_time)printf("ABS time diff usecs: %li\n", labs(u_buf.usec)); //-(long)(dif[n]/2));

        n++;
        if (n == DIF_ARR_SIZE)n = 0;

        if (usecs > 0)
            usleep(usecs);



    }
    return 0;
}

int udp_client_max_sync(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs, int get_remote_time) {
    int sock;
    union time_usec_buf u_buf;
    //    struct timeval local_time;

    //    int bytes_read;
    memset(&u_buf, 0, LOAD_SIZE);

    int n = 0;
    time_t prev_tv_sec;
    long count;

    long dif[DIF_ARR_SIZE] = {0};
    long dif_sorted[DIF_ARR_SIZE] = {0};

    //memset(dif,0,DIF_ARR_SIZE*sizeof(long));
    //memset(dif_sorted,0,DIF_ARR_SIZE*sizeof(long));

    /*for (int i = 0; i < 1000; i++) {
        dif[i] = 0;
        dif_sorted[i] = 0;
    }*/
    count = 0;
    prev_tv_sec = 0;


    struct timespec start, end;

    setbuf(stdout, NULL);

    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind local addr");
        exit(2);
    }

    fcntl(sock, F_SETFL, O_NONBLOCK);
    while (keepRunning) {


        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)perror("clock_gettime start");
        while (keepRunning) {
            errno = 0;
            int k = sendto(sock, &u_buf, LOAD_SIZE, 0, res_remote->ai_addr, res_remote->ai_addrlen);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto");
                    return -1;
                }
            } else {
                break;
            }


        }


        while (keepRunning) {
            errno = 0;
            int k = recvfrom(sock, &u_buf, LOAD_SIZE, 0, NULL, NULL);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("recvfrom");
                    return -1;
                }
            } else {
                if (clock_gettime(CLOCK_MONOTONIC, &end) < 0)perror("clock_gettime end");
                break;
            }
        }
        ++count;



        dif[n] = ((end.tv_sec * (long) 1e9 + end.tv_nsec)-(start.tv_sec * (long) 1e9 + start.tv_nsec)) / 1000;

        if (prev_tv_sec != end.tv_sec) {
            //for (int i = 0; i < 1000; i++)dif_sorted[i] = dif[i];
            memcpy(dif_sorted, dif, DIF_ARR_SIZE);
            qsort(dif_sorted, DIF_ARR_SIZE, sizeof (long), long_cmp);
            printf("Sync send count %li per sec (MEAN %4li usecs)\n", count, dif_sorted[0] > 0 ? dif_sorted[(int) (DIF_ARR_SIZE / 2)] : -1);
            prev_tv_sec = end.tv_sec;
            count = 0;
        }

        n++;
        if (n == DIF_ARR_SIZE)n = 0;

        if (usecs > 0)
            usleep(usecs);
    }
    return 0;
}

int udp_server_max(struct addrinfo *res_local, int push_local_time) {
    int sock;
    //    int bytes_read;
    socklen_t addr_size;
    union time_usec_buf u_buf;

    struct sockaddr remote_client_addr;

    setbuf(stdout, NULL);

    sock = socket(res_local->ai_family, res_local->ai_socktype, res_local->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind");
        exit(2);
    }

    addr_size = sizeof remote_client_addr;
    fcntl(sock, F_SETFL, O_NONBLOCK);

    while (keepRunning) {
        errno = 0;
        int k = recvfrom(sock, &u_buf, LOAD_SIZE, 0, &remote_client_addr, &addr_size);
        if (k < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            } else {
                perror("recvfrom");
                return -1;
            }
        }


        while (keepRunning) {
            errno = 0;
            int k = sendto(sock, &u_buf, LOAD_SIZE, 0, &remote_client_addr, addr_size);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto");
                    return -1;
                }
            } else {
                break;
            }
        }//while
    }

    return 0;


}

int udp_server(struct addrinfo *res_local, int push_local_time) {
    int sock;
    //    int bytes_read;
    socklen_t addr_size;
    union time_usec_buf u_buf;
    struct timeval local_time;

    struct sockaddr remote_client_addr;

    setbuf(stdout, NULL);

    sock = socket(res_local->ai_family, res_local->ai_socktype, res_local->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind");
        exit(2);
    }

    addr_size = sizeof remote_client_addr;
    fcntl(sock, F_SETFL, O_NONBLOCK);

    while (keepRunning) {
        int k = recvfrom(sock, &u_buf, LOAD_SIZE, 0, &remote_client_addr, &addr_size);
        if (k < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            } else {
                perror("recvfrom()");
                return -1;
            }
        } else {
            break;
        }

        if (push_local_time) {
            gettimeofday(&local_time, NULL);
            u_buf.usec = local_time.tv_sec * (int64_t) 1e6 + local_time.tv_usec;
        }

        while (keepRunning) {
            int k = sendto(sock, &u_buf, LOAD_SIZE, 0, &remote_client_addr, addr_size);
            if (k < 1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                } else {
                    perror("sendto()");
                    return -1;
                }
            } else {
                break;
            }
        }
        printf(".");
    }

    return 0;


}

int udp_client_max_throughput(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs) {
    int sock;
    unsigned char *buf = calloc(BYTES_LOAD, sizeof (unsigned char));
    //memset(&buf, 0, LOAD_SIZE);


    //    time_t prev_tv_sec=0;
    //    long count=0;

    //    struct timespec start;

    //setbuf(stdout, NULL);

    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind local addr");
        exit(2);
    } else
        perror("bind local addr");


    printf("BYTES_LOAD %i\n", BYTES_LOAD);

    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    
    
    
    
    
    struct msghdr msg={0};
        struct iovec iov[1];
        iov[0] .iov_base = (void*) buf;
        iov[0] .iov_len = BYTES_LOAD;

        msg.msg_name = (void *) res_remote->ai_addr;
        msg.msg_namelen = res_remote->ai_addrlen;

        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        
        
        
        
        
    while (keepRunning) {


        //if(clock_gettime(CLOCK_MONOTONIC, &start)<0)perror("clock_gettime start");
        while (keepRunning) {
            
            errno = 0;
        int k = sendmsg(sock, &msg, 0);
        
        if (k < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                perror("sendmsg!");
            } else {
                perror("sendmsg?");
                return -1;
            }
        }else
            break;

        }

        ++global_count_snd;

        if (usecs > 0)
            usleep(usecs);


    }
    return 0;
}

int udp_client_max_throughput_prof(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs) {
    /*
     * это функция замера времени системного вызова отправки
     * печатает раз в секунду или близкое к тому медиану и максиимум
     */
    int sock;
    unsigned char *buf = calloc(BYTES_LOAD, sizeof (unsigned char));
    //memset(&buf, 0, LOAD_SIZE);


    //    time_t prev_tv_sec=0;
    //    long count=0;

    //    struct timespec start;

    //setbuf(stdout, NULL);

    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind local addr");
        exit(2);
    } else
        perror("bind local addr");


    

    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    
    
    
    
    
    struct msghdr msg={0};
        struct iovec iov[1];
        iov[0] .iov_base = (void*) buf;
        iov[0] .iov_len = BYTES_LOAD;

        msg.msg_name = (void *) res_remote->ai_addr;
        msg.msg_namelen = res_remote->ai_addrlen;

        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        
        
        
        
    struct timespec start, end;
    int arr_size=1000000;
    if(usecs>0)
        arr_size=1000000/usecs;
    printf("SLEEP      %u\n", usecs);
    printf("BYTES_LOAD %i\n", BYTES_LOAD);
    printf("ARR_SIZE   %i\n", arr_size);
    
    
    
    long *dif = calloc(arr_size,sizeof(long));
    
    int n=0;
    
    while (keepRunning) {


        //if(clock_gettime(CLOCK_MONOTONIC, &start)<0)perror("clock_gettime start");
        while (keepRunning) {
            
            clock_gettime(CLOCK_MONOTONIC, &start);
            errno = 0;
        //int k = sendmsg(sock, &msg, 0);
        int k = sendto(sock, buf, BYTES_LOAD, 0, res_remote->ai_addr, res_remote->ai_addrlen);
        
        if (k < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                perror("sendmsg!");
            } else {
                perror("sendmsg?");
                return -1;
            }
        }else
            break;

        }
        clock_gettime(CLOCK_MONOTONIC, &end);

        
        
        
        dif[n] = ((end.tv_sec * (long) 1e9 + end.tv_nsec)-(start.tv_sec * (long) 1e9 + start.tv_nsec)) / 1000;



        
        
            
            
        
        
        
        
        n++;
        if (n == arr_size){
            n = 0;
            qsort(dif, arr_size, sizeof (long), long_cmp);
            long sum=0;
            for(int x=0;x<arr_size;x++)
                sum+=dif[x];
            double avg=(double)sum/(double)arr_size;
            printf("Last %6i sends MEAN %4li, \tAVG %4.3f, \tP99 %4li, \t MAX %4li\n", arr_size, dif[(int) (arr_size / 2)],avg, dif[(int) (arr_size * 0.99)], dif[arr_size -1]);
        
        }
        ++global_count_snd;

        if (usecs > 0)
            usleep(usecs);


    }
    return 0;
}

int udp_server_max_throughput_unif(struct addrinfo *res_local, struct addrinfo *res_remote, int is_multicast) {
    int sock;

    if (is_multicast) {//если мультикаст, то используем оба адреса: local & remote

        //адрес приема - мультикасм IP
        sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

        //адрес интерфейса - локальный IP
        if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
            perror("bind local addr for multicast recv");
            exit(EXIT_FAILURE);
        }

        //Я умею только с IPv4
        if (res_remote->ai_family == PF_INET && res_remote->ai_addrlen == sizeof (struct sockaddr_in)) /* IPv4 */ {
            struct ip_mreq multicastRequest; /* Multicast address join structure */

            /* Specify the multicast group */
            memcpy(&multicastRequest.imr_multiaddr, &((struct sockaddr_in*) (res_remote->ai_addr))->sin_addr, sizeof (multicastRequest.imr_multiaddr));

            /* Accept multicast from any interface */
            //multicastRequest.imr_interface.s_addr = htonl(INADDR_ANY);
            multicastRequest.imr_interface.s_addr = ((struct sockaddr_in*) (res_local->ai_addr))->sin_addr.s_addr;

            /* Join the multicast address */
            if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &multicastRequest, sizeof (multicastRequest)) != 0) {
                perror("setsockopt(IP_ADD_MEMBERSHIP) failed");
                exit(EXIT_FAILURE);
            }
        } else {
            perror("setsockopt(IP_ADD_MEMBERSHIP) - ai_family not IPv4");
            exit(EXIT_FAILURE);
        }
    } else {
        sock = socket(res_local->ai_family, res_local->ai_socktype, res_local->ai_protocol);
        if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
            perror("bind local addr for unicast recv");
            exit(2);
        }

    }
    
        int buff_bytes = SOKET_BUFFER_SIZE;
        //int buff_bytes = 1500;
        socklen_t sizeof_buff_bytes=sizeof(buff_bytes);
        int buff_bytes_actual = 0;
    
        int result = setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buff_bytes, sizeof_buff_bytes);
        if (result < 0) {
            perror("setsockopt sets the send buffer size");
            exit(EXIT_FAILURE);
        }
        result = getsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buff_bytes_actual, &sizeof_buff_bytes);
        if (result < 0) {
            perror("setsockopt sets the send buffer size");
            exit(EXIT_FAILURE);
        }else{
            if(buff_bytes_actual<buff_bytes)
                fprintf(stderr,"Fail to set SO_SNDBUF size. Ask for %i, but current is %i\n",buff_bytes,buff_bytes_actual);
        }
        
        result = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buff_bytes, sizeof_buff_bytes);
        if (result < 0) {
            perror("setsockopt sets the send buffer size");
            exit(EXIT_FAILURE);
        }
        result = getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buff_bytes_actual, &sizeof_buff_bytes);
        if (result < 0) {
            perror("setsockopt sets the send buffer size");
            exit(EXIT_FAILURE);
        }else{
            if(buff_bytes_actual<buff_bytes)
                fprintf(stderr,"Fail to set SO_RCVBUF size. Ask for %i, but current is %i\n",buff_bytes,buff_bytes_actual);
        }


    fcntl(sock, F_SETFL, O_NONBLOCK);


#ifndef __CYGWIN__
    //Массовое вычитывание (LINUX)
    //больше 20 за раз не вычитывает
//#define vLen 20
    int vLen = mmsghdr_size;
    struct mmsghdr msgs[vLen];
    struct iovec iovecs[vLen];
    //unsigned char bufs[vLen][BYTES_LOAD]; //BYTES_LOAD не макрос!! это стремно
    unsigned char *bufs = calloc(vLen*BYTES_LOAD, sizeof (unsigned char));
    memset(msgs, 0, sizeof (msgs));

    printf("Recive at ones max %i msg, BYTES_LOAD %i [%s]\n", vLen, BYTES_LOAD, is_multicast ? "MULTICAST" : "UNICAST");

    for (int i = 0; i < vLen; i++) {
        iovecs[i].iov_base = bufs + i*BYTES_LOAD;
        iovecs[i].iov_len = BYTES_LOAD;
        msgs[i].msg_hdr.msg_iov = &iovecs[i];
        msgs[i].msg_hdr.msg_iovlen = 1;
        
    }

    while (keepRunning) {
        errno = 0;
        int r = recvmmsg(sock, msgs, vLen, MSG_WAITFORONE, NULL);

        if (r <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            perror("recvmmsg()");
            continue;
        }
        global_count_rcv += r;
    }
    //LINUX END
#else
    //WIN START    
    unsigned char *buf = calloc(BYTES_LOAD, sizeof (unsigned char));
    while (keepRunning) {
        errno = 0;
        int r = recvfrom(sock, &buf, LOAD_SIZE, 0, NULL, NULL);
        if (r < 1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            } else {
                perror("recvfrom()");
                return -1;
            }

        }

        ++global_count_rcv;

    }
#endif    

    return 0;
}

/* заменил на универсальную с макросом
int udp_server_max_throughput (struct addrinfo *res_local, int push_local_time) {
    int sock;
    int bytes_read;
    socklen_t addr_size;
    union time_usec_buf u_buf;

    struct sockaddr remote_client_addr;

    sock = socket(res_local->ai_family, res_local->ai_socktype, res_local->ai_protocol);

    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("bind");
        exit(2);
    }

    addr_size = sizeof remote_client_addr;
    fcntl(sock, F_SETFL, O_NONBLOCK);

    while (keepRunning) {
        bytes_read = recvfrom(sock, &u_buf, LOAD_SIZE, 0, &remote_client_addr, &addr_size);
        if(bytes_read<1){
            if(errno==EAGAIN){
                continue;
            }
            else {
                perror("recvfrom");
                return -1;
            }
        
        }
        ++global_count;
        


    }

    return 0;


}
//*/

//отдельным потоком раз в сек по слипу

void* print_stat_every_sec_thread(void *OL) {
    while (keepRunning) {
        sleep(1);
        printf("snd %li, rcv %li\n", global_count_snd, global_count_rcv);
        global_count_snd = 0;
        global_count_rcv = 0;
    }
    return NULL;
}

//для сотировки diff массивов

int long_cmp(const void *a, const void *b) {
    const long *ia = (const long *) a; // casting pointer types 
    const long *ib = (const long *) b;
    return *ia - *ib;
    /* integer comparison: returns negative if b > a 
     * 	and positive if a > b */
}

//обработка ctrl+c

void intHandler(int dummy) {
    keepRunning = 0;
    hardBrake++;
    if (hardBrake > 1)exit(0);
}

int tcp_server(struct addrinfo *res)//(char *LocalAddr,int port)
{
    int remote_client, listener;
    unsigned char buf[LOAD_SIZE];
    int bytes_read;



    setbuf(stdout, NULL);


    listener = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listener < 0) {
        perror("socket");
        exit(1);
    }
    int flag = 1;
    int result = setsockopt(listener, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof (int));
    if (result < 0) {
        perror("setsockopt TCP_NODELAY on listener");
        //exit(1);

    }
    result = setsockopt(listener, IPPROTO_TCP, TCP_QUICKACK, (char *) &flag, sizeof (int));
    if (result < 0) {
        perror("setsockopt TCP_QUICKACK on listener");
        //exit(1);
    }

    if (bind(listener, res->ai_addr, res->ai_addrlen) < 0) {
        perror("bind");
        exit(2);
    }

    listen(listener, 1);

    while (keepRunning) {
        fprintf(stderr, "Ready, steady, listen\n");
        //blocking accept
        remote_client = accept(listener, NULL, NULL);
        if (remote_client < 0) {
            perror("accept");
            exit(3);
        }
        errno=0;
        int r = fcntl(remote_client, F_SETFL, O_NONBLOCK);
        //if(r<0)
        perror("fcntl O_NONBLOCK");
        
        int flag = 1;

        //int result2 = setsockopt(remote_client, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof (int));
        int result2 = setsockopt(remote_client, SOL_TCP, TCP_NODELAY, &flag, sizeof(flag));
        if (result2 < 0) {
            perror("setsockopt TCP_NODELAY on client");
            //exit(1);
        }
        result2 = setsockopt(remote_client, IPPROTO_TCP, TCP_QUICKACK, (char *) &flag, sizeof (int));
        if (result2 < 0) {
            perror("setsockopt TCP_QUICKACK on client");
            //exit(1);
        }

        //что-то тут некрасово!!
        while (keepRunning) {
            errno = 0;
            bytes_read = recv(remote_client, buf, LOAD_SIZE, 0);
            if(bytes_read == -1){
                if(errno==EAGAIN)
                    continue;
                else{
                    fprintf(stderr,"recv -1! %s\n",strerror(errno));
                    continue;
                }
            }
            else if (bytes_read == 0) {
                fprintf(stderr, "read %d. Close\n", bytes_read);
                break;
            } else if (bytes_read == LOAD_SIZE) {

                //uint64_t *seq_num_p = ((u_int64_t *) & buf[0]);
                //*seq_num_p += 1;
                
                send(remote_client, buf, bytes_read, 0);
                //printf(".");
            }
            else if(bytes_read!=LOAD_SIZE){
                    fprintf(stderr,"read %i: %s\n",bytes_read,strerror(errno));
                    
            }
            else
                fprintf(stderr,"read %i, errno %i\n",bytes_read,errno);
        }

        close(remote_client);
    }
    return 0;
}

int tcp_client(struct addrinfo *res_local, struct addrinfo *res_remote, unsigned int usecs)//(char *ToAddr,char *FromAddr, int port,unsigned int usecs)
{
    int sock;
    int n = 0;
    
    //struct sockaddr_in addr;
    //unsigned char buf[8] = "1234567\0";
    unsigned char buf[8] = {0};

    u_int64_t seq_num=0;
    
    long dif[DIF_ARR_SIZE] = {0};
    long dif_s[DIF_ARR_SIZE] = {0};
    struct timespec start, end;



    sock = socket(res_remote->ai_family, res_remote->ai_socktype, res_remote->ai_protocol);

    if (sock < 0) {
        perror("socket");
        exit(1);
    }


    int flag = 1;
    //int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *) &flag, sizeof (int));
    int result = setsockopt(sock, SOL_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    if (result < 0) {
        perror("setsockopt TCP_NODELAY");
        exit(1);

    }
    result = setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, (char *) &flag, sizeof (int));
    if (result < 0) {
        perror("setsockopt TCP_QUICKACK");
        exit(1);

    }


    if (bind(sock, res_local->ai_addr, res_local->ai_addrlen) < 0) {
        perror("Bind local addr");
        exit(2);
    }

    if (connect(sock, res_remote->ai_addr, res_remote->ai_addrlen) < 0) {
        perror("connect");
        exit(2);
    }
    int r = fcntl(sock, F_SETFL, O_NONBLOCK);
    //if(r<0)
    perror("fcntl O_NONBLOCK");
    while (keepRunning) {
        
        /*
        uint64_t *seq_num_p = ((u_int64_t *) & buf[0]);
        *seq_num_p = seq_num;
        seq_num+=1;
        //*/
        clock_gettime(CLOCK_MONOTONIC, &start);

        int a = send(sock, buf, LOAD_SIZE, 0);
        if (a != LOAD_SIZE)fprintf(stderr, "Send %i bytes, but should be %i\n", a, LOAD_SIZE);
        int b = 0;
        while (keepRunning) {
            errno=0;
            b = recv(sock, buf, LOAD_SIZE, 0);
            if(b == -1){
                if(errno==EAGAIN)
                    continue;
                else{
                    fprintf(stderr,"recv -1! %s\n",strerror(errno));
                    continue;
                }
            }
            else if (b == 0){
                keepRunning = 0;
                fprintf(stderr,"recv 0! Finish");
                break;
            }
            else if (b != LOAD_SIZE)
                fprintf(stderr,"recv less (%i)!\n",b);
            else if (b == LOAD_SIZE)
                break;
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        //tn=(union bl)getMicrotime();
        /*a=(long)tn.l;
        b=(long)tx.l;*/
        //dif[n]=tn.l-tx.l;
        {
            uint64_t *seq_num_p = ((u_int64_t *) & buf[0]);
        if(*seq_num_p != seq_num){
                fprintf(stderr,"recv vrong seq_num: %" PRIu64 " but expecting %" PRIu64 "!\n",*seq_num_p,seq_num);
                for(int i=0;i<8;i++)
                    fprintf(stderr,"%u ",*((u_int8_t *) & buf[i]));
                fprintf(stderr,"\n");
        }
            

        }

        dif[n] = ((end.tv_sec * (long) 1e9 + end.tv_nsec)-(start.tv_sec * (long) 1e9 + start.tv_nsec)) / 1000;



        if(dif[DIF_ARR_SIZE-1]>0){
        //memset(dif_s,0,sizeof (long)*DIF_ARR_SIZE);
        //memcpy(dif_s, dif, DIF_ARR_SIZE);
            for(int k=0;k<DIF_ARR_SIZE;k++){dif_s[k]=dif[k];}
        qsort(dif_s, DIF_ARR_SIZE, sizeof (long), long_cmp);
        printf("Last %4li usecs (MEAN %4li usecs)\n", dif[n], dif_s[(int) (DIF_ARR_SIZE / 2)]);
        }
        else 
        printf("Last %4li usecs \n", dif[n]);
        /*if(dif_s[(int) (DIF_ARR_SIZE / 2)]>100){
        printf("!\n");
        memset(dif_s,0,sizeof (long)*DIF_ARR_SIZE);
        memcpy(dif_s, dif, DIF_ARR_SIZE);
        for(int k=0;k<DIF_ARR_SIZE;k++){dif_s[k]=dif[k];}
        qsort(dif_s, DIF_ARR_SIZE, sizeof (long), long_cmp);
        
        }*/
        
        n++;
        if (n == DIF_ARR_SIZE)n = 0;
        
        if (usecs > 0)
            usleep(usecs);
    }
    close(sock);
    return 0;
}

void help(char *s) {
    fprintf(stderr, "Ohh: %s \n", s);

    fprintf(stderr, "Usage: server (only resend data), most params are optional\n");
    fprintf(stderr, "\tnet_lat -s (upd|tcp|time|udp_max|udp_max_throughput) [-p <port>] [-I <interfaceIP>]\n");
    fprintf(stderr, "Usage: client (calc stat)\n");
    fprintf(stderr, "\tnet_lat -c (upd|tcp|time|udp_max|udp_max_throughput) [-p <port>] [-I <interfaceIP>] [-D <delay_usec>] <remoteIP>\n");
    fprintf(stderr, "udp - network latancy by UDP transport\n");
    fprintf(stderr, "tcp - network latancy by TCP transport\n");
    fprintf(stderr, "time - local wall-clock client-server diff, UDP transport\n");
    fprintf(stderr, "udp_max - max throughput and network latancy by UDP transport (sync send-reply)\n");
    fprintf(stderr, "udp_max_async - max throughput and network latancy by UDP transport (sync send-reply)\n");
    fprintf(stderr, "udp_max_throughput  - max throughput by UDP transport (one direction from client to server)\n");
    fprintf(stderr, "Limitation: both need same arch (byte order) and rending-reciving is linear\n");
}


//**************************************************************************

int main(int argc, char *argv[]) {
    signal(SIGINT, intHandler);

    struct addrinfo hints_local, hints_remote, *res_local, *res_remote;





    if (argc == 1) {
        help("too fiew parameters");
        exit(EXIT_FAILURE);
    }



    //net_lat -s upd|tcp|time -p 123 <localIP>
    //net_lat -c upd|tcp|time -p 123 <remoteIP> <localIP> 
    char *shortOpts = "hc:s:p:I:D:B:G:L:O:";
    int getoptRet, status;
    int role = -1; //0 - client; 1 - server; -1 not set
    int type = -1; //0 - udp; 1 - tcp; 2 - time
    char *port = PORT;
    char *source_ip = "0.0.0.0";
    char *multicast_ip = "0.0.0.0";
    unsigned int usecs = USEC;
    unsigned int usecs_is_def = 1;
    char *types[] = {"udp", "tcp", "time", "udp_max", "udp_max_throughput", "udp_max_async","udp_max_throughput_prof"};
    char *roles[] = {"client", "server"};
    int is_multicast = 0;



    while (-1 != (getoptRet = getopt(argc, argv, shortOpts))) {
        switch (getoptRet) {
            case 'I':
            {
                if (strncmp(optarg, "127.0", 5) == 0) {
                    fprintf(stderr, "Cannot bind to loop! Skeep option.\n");
                    break;
                }
                source_ip = optarg;
                break;
            }
            case 'c':
            {
                role = 0;
                if (strcmp(optarg, "tcp") == 0) type = 1;
                else if (strcmp(optarg, "udp") == 0) type = 0;
                else if (strcmp(optarg, "time") == 0) type = 2;
                else if (strcmp(optarg, "udp_max") == 0) type = 3;
                else if (strcmp(optarg, "udp_max_async") == 0) type = 5;
                else if (strcmp(optarg, "udp_max_throughput") == 0) type = 4;
                else if (strcmp(optarg, "udp_max_throughput_prof") == 0) type = 6;
                else {
                    help("wrong type on clinet role");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 's':
            {
                role = 1;
                if (strcmp(optarg, "tcp") == 0) type = 1;
                else if (strcmp(optarg, "udp") == 0) type = 0;
                else if (strcmp(optarg, "time") == 0) type = 2;
                else if (strcmp(optarg, "udp_max") == 0) type = 3;
                else if (strcmp(optarg, "udp_max_async") == 0) type = 5;
                else if (strcmp(optarg, "udp_max_throughput") == 0) type = 4;
                
                else {
                    help("wrong type on server role");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'p':
            {
                port = optarg;
                break;
            }
            case 'G':
            {
                is_multicast = 1;
                multicast_ip = optarg;
                break;
            }
            case 'D':
            {
                errno = 0;
                usecs = atoi(optarg);
                if (errno != 0) {
                    perror("atoi(usecs)");
                    exit(EXIT_FAILURE);
                }
                usecs_is_def = 0;
                break;
            }
            case 'B':
            {
                errno = 0;
                BYTES_LOAD = atoi(optarg); //only for udp_max_throughput
                if ((errno != 0) || (BYTES_LOAD < 1)) {
                    perror("atoi(BYTES_LOAD)");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'L': //mass read udp - length of Queue
            {
                errno = 0;
                mmsghdr_size = atoi(optarg); //only for udp_max_throughput
                if ((errno != 0) || (mmsghdr_size < 1)) {
                    perror("atoi(mmsghdr_size)");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'O': //snd and rcv buff size for setopt
            {
                errno = 0;
                SOKET_BUFFER_SIZE = atoi(optarg); //only for udp_max_throughput
                if ((errno != 0) || (SOKET_BUFFER_SIZE < 1)) {
                    perror("atoi(SOKET_BUFFER_SIZE)");
                    exit(EXIT_FAILURE);
                }
                break;
            }
            case 'h':
            case '?':
            default:
                break;
        }
    }


    if ((type == -1) || (role == -1)) {
        help("role or type not set");
        exit(EXIT_FAILURE);
    }
    if ((role == 0)&&(optind >= argc)) {
        help("Clinet mode, but target ip not passed\n");
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "%s %s, port %s, delay %i usecs, source_ip %s%s", types[type], roles[role], port, usecs, source_ip, role ? "\n" : ", ");
    if (role == 0)fprintf(stderr, "target_ip '%s'\n", argv[optind]);

    memset(&hints_local, 0, sizeof hints_local);
    memset(&hints_remote, 0, sizeof hints_remote);
    hints_local.ai_family = AF_UNSPEC;
    hints_remote.ai_family = AF_UNSPEC;
    hints_local.ai_socktype = SOCK_DGRAM; //def value
    hints_remote.ai_socktype = SOCK_DGRAM; //def value



    if (role == 0)//client
    {
        if (type == 1) {
            hints_local.ai_socktype = SOCK_STREAM;
            hints_remote.ai_socktype = SOCK_STREAM;
        }

        if ((status = getaddrinfo(source_ip, "0", &hints_local, &res_local)) != 0) {
            //if(errno!=0){printf("getaddrinfo local fails\n");perror("getaddrinfo local");
            fprintf(stderr, "getaddrinfo source_ip error: %s\n", gai_strerror(status));
            exit(EXIT_FAILURE);
        }


        if ((status = getaddrinfo(argv[optind], port, &hints_remote, &res_remote)) != 0) {
            fprintf(stderr, "getaddrinfo remote_ip error: %s\n", gai_strerror(status));
            exit(EXIT_FAILURE);
        }

        if (type == 1)tcp_client(res_local, res_remote, usecs);
        if (type == 0)udp_client(res_local, res_remote, usecs, 0);
        if (type == 2)udp_client(res_local, res_remote, usecs, 1);
        if (type == 3)udp_client_max_sync(res_local, res_remote, usecs, 0);
        if (type == 4) {
            pthread_t PS_pth;
            pthread_attr_t pth_attrs_ps;

            pthread_attr_init(&pth_attrs_ps);
            pthread_attr_setstacksize(&pth_attrs_ps, THREADSTACK);


            if (pthread_create(&PS_pth, &pth_attrs_ps, print_stat_every_sec_thread, NULL) == -1) {
                perror("print_stat_every_sec_thread create");
                exit(EXIT_FAILURE);
            }
            pthread_setname_np(PS_pth, "print_stat");
            //perror("print_stat_every_sec_thread pthread_setname");
            //если usecs не задан, то конкретно в этом случае жарим по полной
            if (usecs_is_def)
                usecs = 0;
            udp_client_max_throughput(res_local, res_remote, usecs);
        }
        if (type == 5)udp_client_max_async_snd(res_local, res_remote, usecs);
        if(type == 6){
            //udp_client_max_throughput_prof
            udp_client_max_throughput_prof(res_local, res_remote, usecs);
        
        }




    } else if (role == 1)//server
    {

        if (type == 1) {
            hints_local.ai_socktype = SOCK_STREAM;
        }

        if ((status = getaddrinfo(source_ip, port, &hints_local, &res_local)) != 0) {
            fprintf(stderr, "getaddrinfo source_ip error: %s\n", gai_strerror(status));
            exit(EXIT_FAILURE);
        }


        if (type == 1)tcp_server(res_local);
        if (type == 0)udp_server(res_local, 0);
        if (type == 2)udp_server(res_local, 1);
        if (type == 3)udp_server_max(res_local, 0);
        if (type == 4) {

            if (is_multicast) {
                if ((status = getaddrinfo(multicast_ip, port, &hints_remote, &res_remote)) != 0) {
                    fprintf(stderr, "getaddrinfo multicast remote_ip error: %s\n", gai_strerror(status));
                    exit(EXIT_FAILURE);
                }
            }


            pthread_t PS_pth;
            pthread_attr_t pth_attrs_ps;

            pthread_attr_init(&pth_attrs_ps);
            pthread_attr_setstacksize(&pth_attrs_ps, THREADSTACK);

            if (pthread_create(&PS_pth, &pth_attrs_ps, print_stat_every_sec_thread, NULL) == -1) {
                perror("print_stat_every_sec_thread create");
                exit(EXIT_FAILURE);
            }
            pthread_setname_np(PS_pth, "print_stat");

            //res_remote содержит группу если мы с флогом -M
            udp_server_max_throughput_unif(res_local, res_remote, is_multicast);
        }
        if (type == 5)udp_server_max_fifo_rcv(res_local);
    }

    exit(EXIT_SUCCESS);


}
