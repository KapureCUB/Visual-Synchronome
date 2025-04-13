/**
*  
* This is the main file for the Visual Synchronome project. 
*
* This program can be used and distributed without restrictions.
*
* Author: Deepak E Kapure
* Project: Visual Synchronome (ECEN 5623 - Real-time Embedded Systems)
*
*/
#define _GNU_SOURCE

#include "../includes/circular_buff.h"
#include "../includes/framecapture.h"
#include "../includes/sequencer.h"

#define FRAME_COUNTS                 (100)
#define NUM_THREADS                  (4)
#define NUM_CPU_CORES                (4)
#define SEQUENCER_EXECUTION_CYCLES   (2000)

// Global variables 
int fd = -1;                                      // file descriptor 
char *dev_name;                                   // device name place holder
struct v4l2_buffer *capture_buf;                  // capture buffer pointer
struct timespec frame_time;
int unq_cnt = 0;

//////////////////
unsigned char *previous_frame;
unsigned char *new_frame;

unsigned long long sequencePeriods;
sem_t semS1, semS2, semS3, semS4;

// thread variables
pthread_t threads[NUM_THREADS];
threadParams_t threadParams[NUM_THREADS];
pthread_attr_t rt_sched_attr[NUM_THREADS];
int rt_max_prio, rt_min_prio, cpuidx;
pthread_attr_t main_attr;
pid_t mainpid;

// thread affinity variables
cpu_set_t threadcpu;
cpu_set_t allcpuset;

// scheduler parameters
struct sched_param rt_param[NUM_THREADS];
struct sched_param main_param;

// timing variables
timer_t timer_1;                           // main timer id
struct itimerspec itime = {{1,0}, {1,0}};  // interval timer struct
struct itimerspec last_itime;
extern double start_realtime;              // declared in sequencer  

void print_scheduler(void);

/******************************/
void main(void) {
    struct timespec current_time_val, current_time_res;
    struct timespec start_time_val;
    double current_realtime, current_realtime_res;

    int i, rc, scope, flags=0;

    //global circular buffer 
    cbuff_struct_t *frame_buffer = (cbuff_struct_t *)calloc(QUEUE_DEPTH, sizeof(cbuff_struct_t));
    if (frame_buffer == NULL) {
        syslog(LOG_INFO, "Circular buffer allocation failed!\n");
        exit(-1);
    }

    //init_circular_buffer(frame_buffer);

    printf("ECEN 5623 Realtime Embedded Systems Final project\n");
    syslog(LOG_INFO, "ECEN 5623 Realtime Embedded Systems Final project");
    
    clock_gettime(MY_CLOCK, &start_time_val);     start_realtime=realtime(&start_time_val);
    clock_gettime(MY_CLOCK, &current_time_val);   current_realtime=realtime(&current_time_val);
    clock_getres(MY_CLOCK, &current_time_res);    current_realtime_res=realtime(&current_time_res);
    
    printf("START High Rate Sequencer @ sec=%6.9lf with resolution %6.9lf\n", 
            (current_realtime - start_realtime), current_realtime_res);
    syslog(LOG_CRIT, "START High Rate Sequencer @ sec=%6.9lf with resolution %6.9lf\n", 
                      (current_realtime - start_realtime), current_realtime_res);

    printf("System has %d processors configured and %d available.\n", get_nprocs_conf(), get_nprocs());

    // clear cpuset
    CPU_ZERO(&allcpuset);
    for(i=0; i < NUM_CPU_CORES; i++)
        CPU_SET(i, &allcpuset);
    printf("Using CPUS=%d from total available.\n", CPU_COUNT(&allcpuset));
    syslog(LOG_INFO, "Using CPUS=%d from total available.\n", CPU_COUNT(&allcpuset));

    // initialize the sequencer semaphores
    if (sem_init (&semS1, 0, 0)) { printf ("Failed to initialize S1 semaphore\n"); exit (-1); }
    if (sem_init (&semS2, 0, 0)) { printf ("Failed to initialize S2 semaphore\n"); exit (-1); }
    if (sem_init (&semS3, 0, 0)) { printf ("Failed to initialize S3 semaphore\n"); exit (-1); }
    if (sem_init (&semS4, 0, 0)) { printf ("Failed to initialize S4 semaphore\n"); exit (-1); }

    mainpid=getpid();

    rt_max_prio = sched_get_priority_max(SCHED_FIFO);
    rt_min_prio = sched_get_priority_min(SCHED_FIFO);

    // set SCHED_FIFO as scheduler
    rc=sched_getparam(mainpid, &main_param);
    main_param.sched_priority=rt_max_prio;
    rc=sched_setscheduler(getpid(), SCHED_FIFO, &main_param);
    if(rc < 0) perror("main_param");
    print_scheduler();


    pthread_attr_getscope(&main_attr, &scope);
    if(scope == PTHREAD_SCOPE_SYSTEM)
      printf("PTHREAD SCOPE SYSTEM\n");
    else if (scope == PTHREAD_SCOPE_PROCESS)
      printf("PTHREAD SCOPE PROCESS\n");
    else
      printf("PTHREAD SCOPE UNKNOWN\n");

    printf("rt_max_prio=%d\n", rt_max_prio);
    printf("rt_min_prio=%d\n", rt_min_prio);


    // set thread 1 on core 1. 
    // Highest priority thread on core 2 for frame capture
    CPU_ZERO(&threadcpu);
    cpuidx=(1);
    CPU_SET(cpuidx, &threadcpu);

    rc=pthread_attr_init(&rt_sched_attr[0]);
    rc=pthread_attr_setinheritsched(&rt_sched_attr[0], PTHREAD_EXPLICIT_SCHED);
    rc=pthread_attr_setschedpolicy(&rt_sched_attr[0], SCHED_FIFO);
    rc=pthread_attr_setaffinity_np(&rt_sched_attr[0], sizeof(cpu_set_t), &threadcpu);

    rt_param[0].sched_priority=rt_max_prio;
    pthread_attr_setschedparam(&rt_sched_attr[0], &rt_param[0]);
    threadParams[0].threadIdx=1;
    threadParams[0].global_cbuf=frame_buffer;

    // set thread 2 and 3 on core 3. 
    // Highest priority thread on core 3 for differencing
    CPU_ZERO(&threadcpu);
    cpuidx=(2);                    
    CPU_SET(cpuidx, &threadcpu);

    rc=pthread_attr_init(&rt_sched_attr[1]);
    rc=pthread_attr_setinheritsched(&rt_sched_attr[1], PTHREAD_EXPLICIT_SCHED);
    rc=pthread_attr_setschedpolicy(&rt_sched_attr[1], SCHED_FIFO);
    rc=pthread_attr_setaffinity_np(&rt_sched_attr[1], sizeof(cpu_set_t), &threadcpu);

    rt_param[1].sched_priority=rt_max_prio;
    pthread_attr_setschedparam(&rt_sched_attr[1], &rt_param[1]);
    threadParams[1].threadIdx=2;
    threadParams[1].global_cbuf=frame_buffer;

    // Second highest priority thread on core 3 for frame selection
    CPU_ZERO(&threadcpu);
    cpuidx=(2);
    CPU_SET(cpuidx, &threadcpu);

    rc=pthread_attr_init(&rt_sched_attr[2]);
    rc=pthread_attr_setinheritsched(&rt_sched_attr[2], PTHREAD_EXPLICIT_SCHED);
    rc=pthread_attr_setschedpolicy(&rt_sched_attr[2], SCHED_FIFO);
    rc=pthread_attr_setaffinity_np(&rt_sched_attr[2], sizeof(cpu_set_t), &threadcpu);

    rt_param[2].sched_priority=rt_max_prio-1;
    pthread_attr_setschedparam(&rt_sched_attr[2], &rt_param[2]);
    threadParams[2].threadIdx=3;
    threadParams[2].global_cbuf=frame_buffer;

    // set thread 4 on core 0. 
    // Best effort thread for write-back to memory
    CPU_ZERO(&threadcpu);
    cpuidx=(3);
    CPU_SET(cpuidx, &threadcpu);

    rc=pthread_attr_init(&rt_sched_attr[3]);
    // rc=pthread_attr_setinheritsched(&rt_sched_attr[3], PTHREAD_EXPLICIT_SCHED);
    // rc=pthread_attr_setschedpolicy(&rt_sched_attr[3], SCHED_OTHER);
    rc=pthread_attr_setaffinity_np(&rt_sched_attr[3], sizeof(cpu_set_t), &threadcpu);

    rt_param[3].sched_priority=rt_max_prio;
    pthread_attr_setschedparam(&rt_sched_attr[3], &rt_param[3]);
    threadParams[3].threadIdx=4;
    threadParams[3].global_cbuf=frame_buffer;

    // Create Service threads which will block awaiting release for:
    // Servcie_1 = RT_MAX-1	@ 33 Hz. Frame capture
    rc=pthread_create(&threads[0],               // pointer to thread descriptor
                      &rt_sched_attr[0],         // use specific attributes
                      Service_1,                 // thread function entry point
                      (void *)&(threadParams[0]) // parameters to pass in
                     );
    if(rc < 0)
        perror("pthread_create for service 1");
    else
        printf("pthread_create successful for service 1\n");


    // Service_2 = RT_MAX-2	@ 20 Hz. Differencing
    rc=pthread_create(&threads[1], 
                      &rt_sched_attr[1],
                      Service_2, 
                      (void *)&(threadParams[1])
                      );
    if(rc < 0)
        perror("pthread_create for service 2");
    else
        printf("pthread_create successful for service 2\n");


    // Service_3 = RT_MAX-3	@ 1 Hz. Frame selection
    rc=pthread_create(&threads[2], 
                      &rt_sched_attr[2], 
                      Service_3, 
                      (void *)&(threadParams[2])
                      );
    if(rc < 0)
        perror("pthread_create for service 3");
    else
        printf("pthread_create successful for service 3\n");


    // Service_4 = RT_MAX-4, best effort. Write-back
    rc=pthread_create(&threads[3], 
                      &rt_sched_attr[3], 
                      Service_4, 
                      (void *)&(threadParams[3])
                      );
    if(rc < 0)
        perror("pthread_create for service 4");
    else
        printf("pthread_create successful for service 4\n");
 
    // Create Sequencer thread, which like a cyclic executive, is highest prio
    printf("Sequencer thread running on CPU=%d\n", sched_getcpu());
    syslog(LOG_INFO, "Sequencer thread running on CPU=%d", sched_getcpu());

    printf("Start sequencer\n");
    //sequencePeriods=SEQUENCER_EXECUTION_CYCLES;

    // Sequencer = RT_MAX	@ 100 Hz
    /* set up to signal SIGALRM if timer expires */
    timer_create(CLOCK_REALTIME, NULL, &timer_1);
    signal(SIGALRM, (void(*)()) Sequencer);

    /* arm the interval timer */
    itime.it_interval.tv_sec = 0;
    itime.it_interval.tv_nsec = 10000000;
    itime.it_value.tv_sec = 0;
    itime.it_value.tv_nsec = 10000000;

    timer_settime(timer_1, flags, &itime, &last_itime);

    for(i=0;i<NUM_THREADS;i++) {
        if(rc=pthread_join(threads[i], NULL) < 0)
            perror("main pthread_join");
        else
            printf("joined thread %d\n", i);
    }
   
   free(frame_buffer);
   printf("\nTEST COMPLETE\n");
}

void print_scheduler(void) {
   int schedType;

   schedType = sched_getscheduler(getpid());

   switch(schedType)
   {
       case SCHED_FIFO:
           printf("Pthread Policy is SCHED_FIFO\n");
           break;
       case SCHED_OTHER:
           printf("Pthread Policy is SCHED_OTHER\n"); exit(-1);
         break;
       case SCHED_RR:
           printf("Pthread Policy is SCHED_RR\n"); exit(-1);
           break;
       //case SCHED_DEADLINE:
       //    printf("Pthread Policy is SCHED_DEADLINE\n"); exit(-1);
       //    break;
       default:
           printf("Pthread Policy is UNKNOWN\n"); exit(-1);
   }
}