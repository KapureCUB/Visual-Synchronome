// Sam Siewert, December 2020
//
// Sequencer Generic Demonstration
//
// The purpose of this code is to provide an example for how to best
// sequence a set of periodic services in Linux user space without specialized hardware like
// an auxiliary programmable interval timere and/or real-time clock.  For problems similar to and including
// the final project in real-time systems.
//
// AMP Configuration (check core status with "lscpu"):
//
// 1) Uses SCEHD_FIFO - https://man7.org/linux/man-pages//man7/sched.7.html
// 2) Sequencer runs on core 1
// 3) EVEN thread indexes run on core 2
// 4) ODD thread indexes run on core 3
// 5) Linux kernel mostly runs on core 0, but does load balance non-RT workload over all cores
// 6) check for irqbalance [https://linux.die.net/man/1/irqbalance] which also distribute IRQ handlers
//
// What we really want in addition to SCHED_FIFO with CPU core affinity is:
//
// 1) A reliable periodic source of interrupts (emulated by delay in a loop here)
// 2) An accurate (minimal drift) and precise timestamp
//    * e.g. accurate to 1 millisecond or less, ideally 1 microsecond, but not realistic on an RTOS even
//    * overall, what we want is predictable response with some accuracy (minimal drift) and precision
//
// Linux user space presents a challenge because:
//
// 1) Accurate timestamps are either not available or the ASM instructions to read system clocks can't
//    be issued in user space for security reasons (x86 and x64 TSC, ARM STC).
// 2) User space time with clock_gettime is recommended, but still requires the overhead of a system call
// 3) Linux user space is inherently driven by the jiffy and tick as shown by:
//    * "getconf CLK_TCK" - normall 10 msec tick at 100 Hz
//    * cat /proc/timer_list
// 4) Linux kernel space certainly has more accurate timers that are high resolution, but we would have to
//    write our entire solution as a kernel module and/or use custom kernel modules for timekeeping and
//    selected services.
// 5) Linux kernel patches for best real-time performance include RT PREEMPT (http://www.frank-durr.de/?p=203)
// 6) MUTEX semaphores can cause unbounded priority inversion with SCHED_FIFO, so they should be avoided or
//    * use kernel patches for RT semaphore support
//      [https://opensourceforu.com/2019/04/how-to-avoid-priority-inversion-and-enable-priority-inheritance-in-linux-kernel-programming/]
//    * use the FUTEX instead of standard POSIX semaphores
//      [https://eli.thegreenplace.net/2018/basics-of-futexes/]
//    * POSIX sempaphores do have inversion safe features, but they do not work on un-patched Linux distros
//
// However, for our class goals for soft real-time synchronization with a 1 Hz and a 10 Hz external
// clock (and physical process), the user space approach should provide sufficient accuracy required and
// precision which is limited by our camera frame rate to 30 Hz anyway (33.33 msec).
//
// Sequencer - 100 Hz 
//                   [gives semaphores to all other services]
// Service_1 - 50 Hz, every other Sequencer loop
// Service_2 - 20 Hz, every 5th Sequencer loop 
// Service_3 - 10 Hz ,every 10th Sequencer loop
// Service_4 -  5 Hz, every 20th Sequencer loop
// Service_5 -  2 Hz ,every 50th Sequencer loop
// Service_6 -  1 Hz, every 100th Sequencer loop
// Service_7 -  1 Hz, every 100th Sequencer loop
//
// With the above, priorities by RM policy would be:
//
// Sequencer = RT_MAX	@ 100 Hz
// Servcie_1 = RT_MAX-1	@ 50  Hz
// Service_2 = RT_MAX-2	@ 20  Hz
// Service_3 = RT_MAX-3	@ 10  Hz
// Service_4 = RT_MAX-4	@ 5   Hz
// Service_5 = RT_MAX-5	@ 2   Hz
// Service_6 = RT_MAX-6	@ 1   Hz
// Service_7 = RT_MIN	@ 1   Hz
//
/////////////////////////////////////////////////////////////////////////////
// JETSON SYSTEM NOTES:
/////////////////////////////////////////////////////////////////////////////
//
// Here are a few hardware/platform configuration settings on your Jetson
// that you should also check before running this code:
//
// 1) Check to ensure all your CPU cores on in an online state - USE "lscpu"
//
// 2) Check /sys/devices/system/cpu or do lscpu.
//
//    Tegra is normally configured to hot-plug CPU cores, so to make all
//    available, as root do:
//
//    echo 0 > /sys/devices/system/cpu/cpuquiet/tegra_cpuquiet/enable
//    echo 1 > /sys/devices/system/cpu/cpu1/online
//    echo 1 > /sys/devices/system/cpu/cpu2/online
//    echo 1 > /sys/devices/system/cpu/cpu3/online
//
// 3) The Jetson NANO requiress a sysctl setting to allow for SCHED_FIFO to be used:
//
//    sysctl -w kernel.sched_rt_runtime_us=-1
//
//    See - https://forums.developer.nvidia.com/t/pthread-setschedparam-sched-fifo-fails/64394/3
//
// 4) Check for precision time resolution and support with cat /proc/timer_list
//
// 5) Ideally all printf calls should be eliminated as they can interfere with
//    timing.  They should be replaced with an in-memory event logger or at
//    least calls to syslog.
//
// 6) For determinism, you should use CPU affinity for AMP scheduling.  Note that without specific affinity,
//    threads will be SMP by default, annd will be migrated to the least busy core, so be careful.

// This is necessary for CPU affinity macros in Linux
#define _GNU_SOURCE

#include "../includes/sequencer.h"
#include "../includes/circular_buff.h"
#include "../includes/framecapture.h"
#include "../includes/writeback.h"
#include "../includes/differencing.h"

int abortTest=FALSE;
int abortS1=FALSE, abortS2=FALSE, \
    abortS3=FALSE, abortS4=FALSE;

extern sem_t semS1, semS2, semS3, semS4;
double start_realtime;

extern unsigned long long sequencePeriods;
extern timer_t timer_1;
static unsigned long long seqCnt=0;

int delta_t(struct timespec *stop, struct timespec *start, struct timespec *delta_t) {
  int dt_sec=stop->tv_sec - start->tv_sec;
  int dt_nsec=stop->tv_nsec - start->tv_nsec;

  // case 1 - less than a second of change
  if(dt_sec == 0) {
	  if(dt_nsec >= 0 && dt_nsec < NANOSEC_PER_SEC) {
		  delta_t->tv_sec = 0;
		  delta_t->tv_nsec = dt_nsec;
	  } else if(dt_nsec > NANOSEC_PER_SEC) {
		  delta_t->tv_sec = 1;
		  delta_t->tv_nsec = dt_nsec-NANOSEC_PER_SEC;
	  } else {                              // dt_nsec < 0 means stop is earlier than start
	         printf("stop is earlier than start\n");
		 return(-1);  
	  }
  } else if(dt_sec > 0) {                   // case 2 - more than a second of change, check for roll-over
	  if(dt_nsec >= 0 && dt_nsec < NANOSEC_PER_SEC) {
	          //printf("nanosec greater at stop than start\n");
		  delta_t->tv_sec = dt_sec;
		  delta_t->tv_nsec = dt_nsec;
	  } else if(dt_nsec > NANOSEC_PER_SEC) {
	          //printf("nanosec overflow\n");
		  delta_t->tv_sec = delta_t->tv_sec + 1;
		  delta_t->tv_nsec = dt_nsec-NANOSEC_PER_SEC;
	  } else {                               // dt_nsec < 0 means roll over
		  delta_t->tv_sec = dt_sec-1;
		  delta_t->tv_nsec = NANOSEC_PER_SEC + dt_nsec;
	  }
  }
  return(1);
}

// For background on high resolution time-stamps and clocks:
//
// 1) https://www.kernel.org/doc/html/latest/core-api/timekeeping.html
// 2) https://blog.regehr.org/archives/794 - Raspberry Pi
// 3) https://blog.trailofbits.com/2019/10/03/tsc-frequency-for-all-better-profiling-and-benchmarking/
// 4) http://ecee.colorado.edu/~ecen5623/ecen/ex/Linux/example-1/perfmon.c
// 5) https://blog.remibergsma.com/2013/05/12/how-accurately-can-the-raspberry-pi-keep-time/
//
// The Raspberry Pi does not ship with a TSC nor HPET counter to use as clocksource. Instead it relies on
// the STC that Raspbian presents as a clocksource. Based on the source code, “STC: a free running counter
// that increments at the rate of 1MHz”. This means it increments every microsecond.
//
// "sudo apt-get install adjtimex" for an interesting utility to adjust your system clock

void Sequencer(int id) {
    struct timespec current_time_val;
    double current_realtime;
    struct itimerspec itime;
    struct itimerspec last_itime;
    int rc, flags=0;

    seqCnt++;

    //clock_gettime(MY_CLOCK_TYPE, &current_time_val); current_realtime=realtime(&current_time_val);
    // printf("Sequencer on core %d for cycle %llu @ sec=%6.9lf\n", 
    //                                 sched_getcpu(), seqCnt, current_realtime-start_realtime);
    // syslog(LOG_CRIT, "Sequencer on core %d for cycle %llu @ sec=%6.9lf\n", 
    //                                 sched_getcpu(), seqCnt, current_realtime-start_realtime);

    // Release each service at a sub-rate of the generic sequencer rate
    // Servcie_1 = RT_MAX-1	@ 33 Hz
    if((seqCnt % 3) == 0) sem_post(&semS1);

    // Service_2 = RT_MAX-2	@ 20 Hz
    if((seqCnt % 5) == 0) sem_post(&semS2);

    // Service_3 = RT_MAX-3	@ 1 Hz
    //if((seqCnt % 100) == 0) sem_post(&semS3);
    // Service_3 = RT_MAX-3	@ 10 Hz
    if((seqCnt % 10) == 0) sem_post(&semS3);
    
    if(abortTest || (sequencePeriods >= FRAME_CAPTURE_COUNT)) {
        // disable interval timer
        itime.it_interval.tv_sec = 0;
        itime.it_interval.tv_nsec = 0;
        itime.it_value.tv_sec = 0;
        itime.it_value.tv_nsec = 0;

        timer_settime(timer_1, flags, &itime, &last_itime);
	    printf("Disabling sequencer interval timer with abort=%d and %llu of %lld\n", 
                                                   abortTest, seqCnt, sequencePeriods);

	    // shutdown all services
        sem_post(&semS1); sem_post(&semS2); 
        sem_post(&semS3); sem_post(&semS4);

        abortS1=TRUE; abortS2=TRUE; 
        abortS3=TRUE; abortS4=TRUE;
    }

}

void *Service_1(void *threadp) {
    struct timespec current_time_val;
    double current_realtime;
    unsigned long long S1Cnt=0;
    struct timespec prev_time, delay_time;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    int fd = -1;                                      // file descriptor 
    char *dev_name = DEFAULT_VIDEO_DEVICE;            // device name place holder

    printf("S1 33Hz thread running on CPU=%d\n", sched_getcpu());
    syslog(LOG_INFO, "S1 33Hz thread running on CPU=%d", sched_getcpu());

    // Start up processing and resource initialization
    clock_gettime(MY_CLOCK, &current_time_val); current_realtime=realtime(&current_time_val);
    syslog(LOG_CRIT, "S1 33Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);
    printf("S1 33Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);

    // initialization of V4L2
    fd = open_device(dev_name);
    init_device(fd, dev_name);
    start_capturing(fd);

    while(!abortS1) { // check for synchronous abort request

	    // wait for service request from the sequencer, a signal handler or ISR in kernel
        sem_wait(&semS1);
        S1Cnt++;
        
        //print_cbuf_info();
	    read_frames(fd, threadParams->global_cbuf);                                             // capture frame
        
	    // on order of up to milliseconds of latency to get time
        clock_gettime(MY_CLOCK, &current_time_val);     
        current_realtime=realtime(&current_time_val);
        delta_t(&current_time_val, &prev_time, &delay_time);        // calculate capture time
        syslog(LOG_INFO, "S1 33Hz on core %d for release %llu @ sec=%6.9lf frameRate=%f\n", 
                                            sched_getcpu(), S1Cnt, current_realtime-start_realtime,
                                            (1.0/((double)(delay_time.tv_sec) + ((double)(delay_time.tv_nsec) / NANOSEC_PER_SEC))));       
        prev_time.tv_nsec = current_time_val.tv_nsec;
        prev_time.tv_sec = current_time_val.tv_sec;                             
    }

    // shutdown of frame acquisition service
    stop_capturing(fd);
    uninit_device();
    close_device(fd);

    printf("Sequence counts for service 1: %d\n", S1Cnt);
    // Resource shutdown here
    pthread_exit((void *)0);
}

void *Service_2(void *threadp) {
    int ret = 0;
    struct timespec current_time_val;
    double current_realtime;
    unsigned long long S2Cnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    printf("S2 20Hz thread running on CPU=%d\n", sched_getcpu());
    syslog(LOG_INFO, "S2 20Hz thread running on CPU=%d", sched_getcpu());

    clock_gettime(MY_CLOCK, &current_time_val); current_realtime=realtime(&current_time_val);
    syslog(LOG_CRIT, "S2 20Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);
    printf("S2 20Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);

    while(!abortS2) {
        sem_wait(&semS2);
        S2Cnt++;
        
        //print_cbuf_info();
        ret = differencing(threadParams->global_cbuf);

        //printf("Frames serviced in differencing %d\n", ret);
        syslog(LOG_INFO, "Frames serviced in differencing %d\n", ret);
        clock_gettime(MY_CLOCK, &current_time_val);    current_realtime=realtime(&current_time_val);
        syslog(LOG_CRIT, "S2 20 Hz on core %d for release %llu @ sec=%6.9lf\n", 
                                                        sched_getcpu(), S2Cnt, current_realtime-start_realtime);
    }
    printf("Sequence counts for service 2: %d\n", S2Cnt);
    pthread_exit((void *)0);
}

void *Service_3(void *threadp) {
    int ret = 0;
    struct timespec current_time_val;
    double current_realtime;
    unsigned long long S3Cnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    printf("S3 1Hz thread running on CPU=%d\n", sched_getcpu());
    syslog(LOG_INFO, "S3 1Hz thread running on CPU=%d", sched_getcpu());

    clock_gettime(MY_CLOCK, &current_time_val); current_realtime=realtime(&current_time_val);
    syslog(LOG_CRIT, "S3 1Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);
    printf("S3 1Hz thread @ sec=%6.9lf\n", current_realtime-start_realtime);

    //init_fifoQ();

    while(!abortS3) {
        sem_wait(&semS3);
        S3Cnt++;

        ret = frame_select(threadParams->global_cbuf);
        if(ret==-1) {
            printf("Frame select: first_capture not triggered\n");
            syslog(LOG_INFO, "Frame select: first_capture not triggered\n");
        }
        if(ret==0) {
            printf("Frame select: No valid frame selected ");
            print_cbuf_info();
            syslog(LOG_INFO, "Frame select: No valid frame selected\n");
        }
        if(ret>0) {
            printf("Frame select: Valid frames found - %d\n", ret);
            syslog(LOG_INFO, "Frame select: Valid frames found - %d\n", ret);
        }

        clock_gettime(MY_CLOCK, &current_time_val);     current_realtime=realtime(&current_time_val);
        syslog(LOG_CRIT, "S3 10 Hz on core %d for release %llu @ sec=%6.9lf\n", 
                                                        sched_getcpu(), S3Cnt, current_realtime-start_realtime);
    }
    printf("Sequence counts for service 3: %d\n", S3Cnt);
    pthread_exit((void *)0);
}

void *Service_4(void *threadp) {
    int ret = -1;
    struct timespec current_time_val;
    double current_realtime;
    unsigned long long S4Cnt=0;
    threadParams_t *threadParams = (threadParams_t *)threadp;

    printf("S4 best effort thread running on CPU=%d\n", sched_getcpu());
    syslog(LOG_INFO, "S4 best effort thread running on CPU=%d", sched_getcpu());

    clock_gettime(MY_CLOCK, &current_time_val); current_realtime=realtime(&current_time_val);
    syslog(LOG_CRIT, "S4 best effort thread @ sec=%6.9lf\n", current_realtime-start_realtime);
    printf("S4 best effor thread @ sec=%6.9lf\n", current_realtime-start_realtime);

    while(!abortS4) {
        //sem_wait(&semS4);
        S4Cnt++;
        ret = writeback();
        if(ret > -1) {
            //printf("Write-back: %d frame written to memory\n", ret);
            syslog(LOG_INFO, "Write-back: %d frame written to memory\n", ret);
            sequencePeriods += ret;
        }
        //clock_gettime(MY_CLOCK, &current_time_val);     current_realtime=realtime(&current_time_val);
        //syslog(LOG_CRIT, "S4 best effort on core %d for release %llu @ sec=%6.9lf\n", 
                                                        //sched_getcpu(), S4Cnt, current_realtime-start_realtime);
    }
    printf("Sequence counts for service 4: %d\n", S4Cnt);
    pthread_exit((void *)0);
}

double getTimeMsec(void) {
  struct timespec event_ts = {0, 0};

  clock_gettime(MY_CLOCK, &event_ts);
  return ((event_ts.tv_sec)*1000.0) + ((event_ts.tv_nsec)/1000000.0);
}

double realtime(struct timespec *tsptr) {
    return ((double)(tsptr->tv_sec) + (((double)tsptr->tv_nsec)/1000000000.0));
}