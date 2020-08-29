#ifndef TIMERS_H_
#define TIMERS_H_

#include <inttypes.h>
#include <time.h>

/*
USAGE:
    TIMESTAMP_INIT
    TIMESTAMP_T t1, t2;

    GET_TIMESTAMP(t1);
    GET_TIMESTAMP(t2);
    printf("Elapsed time in nanoseconds: %lu\n", ELAPSED_NSEC(t1, t2));
*/

#define RDTSC 1
#define RDTSCP 2
#define GET_TIME_MONOTONIC 3

#define LOG_FUNCTION GET_TIME_MONOTONIC

#if LOG_FUNCTION == RDTSC
#define TIMESTAMP_INIT HRT_INIT
#define TIMESTAMP_T HRT_TIMESTAMP_T
#define GET_TIMESTAMP(t) HRT_GET_TIMESTAMP_UNSAFE(t)
#define ELAPSED_NSEC(t1, t2) HRT_GET_INTERVAL_NSEC(t1, t2)
#elif LOG_FUNCTION == RDTSCP
#define TIMESTAMP_INIT HRT_INIT
#define TIMESTAMP_T HRT_TIMESTAMP_T
#define GET_TIMESTAMP(t) HRT_GET_TIMESTAMP_SAFE(t)
#define ELAPSED_NSEC(t1, t2) HRT_GET_INTERVAL_NSEC(t1, t2)
#elif LOG_FUNCTION == GET_TIME_MONOTONIC
#define TIMESTAMP_INIT
#define TIMESTAMP_T struct timespec
#define GET_TIMESTAMP(t) clock_gettime(CLOCK_MONOTONIC, &t)
#define ELAPSED_NSEC(t1, t2) (t2.tv_nsec + t2.tv_sec * 1000000000UL - t1.tv_nsec - t1.tv_sec * 1000000000UL)
#endif

#define UINT32_T uint32_t
#define UINT64_T uint64_t

typedef struct {
    UINT32_T l;
    UINT32_T h;
} x86_64_timeval_tt;

#define HRT_TIMESTAMP_T x86_64_timeval_tt

#define HRT_GET_TIMESTAMP_UNSAFE(t1) \
    __asm__ __volatile__("rdtsc" : "=a"(t1.l), "=d"(t1.h));
#define HRT_GET_TIMESTAMP_SAFE(t1) \
    __asm__ __volatile__("rdtscp" : "=a"(t1.l), "=d"(t1.h));

#define HRT_GET_ELAPSED_TICKS(t1, t2, numptr) \
    *numptr =                                 \
        ((((UINT64_T)t2.h) << 32) | t2.l) - ((((UINT64_T)t1.h) << 32) | t1.l);

/* global timer frequency in Hz */
unsigned long long g_timerfreq;

#define HRT_INIT                                                   \
    do {                                                                \
        static volatile HRT_TIMESTAMP_T t1, t2;                         \
        static volatile UINT64_T elapsed_ticks, min = (UINT64_T)(~0x1); \
        int notsmaller = 0;                                             \
        while (notsmaller < 3) {                                        \
            printf("Calibrating\n");                                    \
            HRT_GET_TIMESTAMP_SAFE(t1);                                 \
            sleep(1);                                                   \
            GET_TIMESTAMP(t2);                                          \
            HRT_GET_ELAPSED_TICKS(t1, t2, &elapsed_ticks);              \
            notsmaller++;                                               \
            if (elapsed_ticks < min) {                                  \
                min = elapsed_ticks;                                    \
                notsmaller = 0;                                         \
            }                                                           \
        }                                                               \
        g_timerfreq = min;                                              \
        printf("freq %llu\n", g_timerfreq);                             \
    } while (0);

#define HRT_GET_TIME(t1, time) time = ((((UINT64_T)t1.h) << 32) | t1.l)

#define HRT_GET_NSEC(ticks) \
    (unsigned long)(1e9 * (double)ticks / (double)g_timerfreq)

#define HRT_GET_INTERVAL_NSEC(t1, t2)                 \
    HRT_GET_NSEC((((((UINT64_T)t2.h) << 32) | t2.l) - \
                  ((((UINT64_T)t1.h) << 32) | t1.l)))

#endif /* TIMERS_H_ */
