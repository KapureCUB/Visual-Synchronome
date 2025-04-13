/**
*  
* This header contains the helper functions for Circular buffer
*
* This program can be used and distributed without restrictions.
*
* Author: Deepak E Kapure
* Project: Visual Synchronome (ECEN 5623 - Real-time Embedded Systems)
*
*/

#include <stdio.h>  // for printf()
#include <string.h> // for memcpy()
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>            
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "pthread.h"
#include <linux/videodev2.h>
#include "../includes/writeback.h"
#include "../includes/circular_buff.h"
#include "../includes/framecapture.h"
#include "../includes/differencing.h"

// for logging
#include <syslog.h>

char ppm_header[]="P6\n#9999999999 sec 9999999999 msec \n"HRES_STR" "VRES_STR"\n255\n \
                    Linux raspberrypi 6.1.21-v8+ #1642 SMP PREEMPT Mon Apr  3 17:24:16 BST 2023 aarch64 GNU/Linux\n \
                    999 99 999 9999 99999999 99 999";
char ppm_dumpname[]="frames/test0000.ppm";

pthread_mutex_t sgl_fifo;

char buffer[256];
char date_result[1024] = "Sat 10 Aug 2024 06:54:07 PM MDT";                    // To store the final result
FILE *fp;

typedef struct {
    cbuff_struct_t *data[MAX_FIFO_DEPTH];
    //cbuff_struct_t data[MAX_FIFO_DEPTH];
    int front;
    int rear;
    int count;
} fifo_queue_t;

fifo_queue_t fifo_queue;
 
void init_fifoQ(void) {
    memset(&fifo_queue,0,sizeof(fifo_queue_t));
}

static void dump_ppm(cbuff_struct_t *element) {
    int written, i, total, dumpfd;

    unsigned char *p = &(element->buffer[0]);
    int size = element->size;
    unsigned int tag = element->frame_count;
    struct timespec *time = &(element->timestamp);

    // printf("dump ppm: size=%d framecount=%d time=%d\n", size, tag, 
    //                                                 time->tv_sec);

    snprintf(&ppm_dumpname[11], 9, "%04d", tag);
    strncat(&ppm_dumpname[15], ".ppm", 5);
    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    snprintf(&ppm_header[4], 11, "%010d", (int)time->tv_sec);
    strncat(&ppm_header[14], " sec ", 5);
    snprintf(&ppm_header[19], 11, "%010d", (int)((time->tv_nsec)/1000000));
    strncat(&ppm_header[29], " msec \n"HRES_STR" "VRES_STR"\n255\n", 19);
    strncat(&ppm_header[48], "Linux raspberrypi 6.1.21-v8+ #1642 SMP PREEMPT Mon Apr  3 17:24:16 BST 2023 aarch64 GNU/Linux\n", 94);
    strncat(&ppm_header[142], date_result, sizeof(date_result));

    written=write(dumpfd, ppm_header, sizeof(ppm_header));

    total=0;

    syslog(LOG_INFO,"Starting frame writes to memory");

    do {
        written=write(dumpfd, p, size);
        //printf("written bytes %d\n", written);
        total+=written;
    } while(total < size);

    //printf("wrote %d bytes\n", total);
    syslog(LOG_INFO, "wrote %d bytes\n", total);

    close(dumpfd);
}

// Push an element into the queue
int push_frame_fifo(cbuff_struct_t *element) {
    if(fifo_queue.count == MAX_FIFO_DEPTH) {
        // Queue is full
        return -1;
    }
    fifo_queue.data[fifo_queue.rear] = element;
    // memcpy(&(fifo_queue.data[fifo_queue.rear].buffer), element->buffer, element->size);
    // fifo_queue.data[fifo_queue.rear].size = element->size;
    // fifo_queue.data[fifo_queue.rear].frame_count = element->frame_count;
    // fifo_queue.data[fifo_queue.rear].timestamp.tv_sec = element->timestamp.tv_sec;
    // fifo_queue.data[fifo_queue.rear].timestamp.tv_nsec = element->timestamp.tv_nsec;

    // printf("Push: size=%d framecount=%d time=%d\n", fifo_queue.data[fifo_queue.rear].size, fifo_queue.data[fifo_queue.rear].frame_count, 
    //                                                 fifo_queue.data[fifo_queue.rear].timestamp.tv_sec);
    fifo_queue.rear = (fifo_queue.rear + 1) % MAX_FIFO_DEPTH;

    pthread_mutex_lock(&sgl_fifo);
    fifo_queue.count++;
    pthread_mutex_unlock(&sgl_fifo);
    //printf("Push: front=%d rear=%d count=%d\n", fifo_queue.front, fifo_queue.rear, fifo_queue.count);
    return 0;
}

cbuff_struct_t *pop_frame_fifo(void) {
    cbuff_struct_t *ret = NULL;

    if(fifo_queue.count == 0) {
        // Queue is empty
        return NULL;
    }
    ret = fifo_queue.data[fifo_queue.front];
    fifo_queue.front = (fifo_queue.front + 1) % MAX_FIFO_DEPTH;

    pthread_mutex_lock(&sgl_fifo);
    fifo_queue.count--;
    pthread_mutex_unlock(&sgl_fifo);

    //printf("Pop: front=%d rear=%d count=%d\n", fifo_queue.front, fifo_queue.rear, fifo_queue.count);
    return ret;
 }

void get_sys_timestamp(void) {
    fp = popen("date", "r");
    if (fp == NULL) {
        perror("popen failed");
        return;
    }

    // Read the output a line at a time - output it and store it in uname_result
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        strncat(date_result, buffer, sizeof(date_result) - strlen(date_result) - 1);
    }

    // Close the file pointer
    pclose(fp);
}

int writeback(void) {
    int ret = -1;
    cbuff_struct_t *local_data;
    
    local_data = pop_frame_fifo();

    if(local_data != NULL) {
        //print_cbuf_info();
        get_sys_timestamp();
        dump_ppm(local_data);
        printf("Write-back: frame %d written to memory\n", local_data->frame_count);
        syslog(LOG_INFO,"Write-back: frame %d written to memory\n", local_data->frame_count);
        ret = 1;
    }

    return ret;
}