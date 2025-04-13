#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <semaphore.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <errno.h>
#include <signal.h>

#include "../includes/circular_buff.h"

#define TRUE                    (1)
#define FALSE                   (0)

typedef struct {
    int threadIdx;
    cbuff_struct_t *global_cbuf;
} threadParams_t;

void Sequencer(int id);
void *Service_1(void *threadp);
void *Service_2(void *threadp);
void *Service_3(void *threadp);
void *Service_4(void *threadp);

double getTimeMsec(void);
double realtime(struct timespec *tsptr);