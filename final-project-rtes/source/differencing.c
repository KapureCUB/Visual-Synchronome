/**
*  
* This file contains the helper functions for capturing frames
* from the Logitech C270 webcam using V4L2 driver APIs. 
* Part of the program functions were taken from Prof. Sam Siewert's
* simple-capture-1800 sample code.
*
* This program can be used and distributed without restrictions.
*
* Author: Deepak E Kapure
* Project: Visual Synchronome (ECEN 5623 - Real-time Embedded Systems)
*
*/

#include "../includes/circular_buff.h"
#include "../includes/writeback.h"
#include "../includes/differencing.h"
// for logging
#include <syslog.h>
#include <stdio.h>

#define FRAME_DIFF_THRESHOLD         (20)
#define FRAMES_TO_SERVICE            (5)
#define PIXEL_DIFFERENCE_THRESHOLD   (300)
#define FRAME_USEFUL                 (1)
#define FRAME_NOT_USEFUL             (0)

unsigned int frame_count = 1;                    // Frame counts

int size;
bool first_capture = true;
extern unsigned char *previous_frame;
extern unsigned char *new_frame;

//int offset = 45.0;
int offset = 10.0;

int new_ts, old_ts = 0;
struct timespec temp_time;
extern int garbage_frames;
extern int  rptr_diff;                 // read pointer for differencing
extern int  rptr_sel;                 // read pointer for selection

static int perform_diff(unsigned char *new, unsigned char *prev, int size) {
    unsigned long diff_count = 0;

    if(size > MAX_BUFFER_LENGTH)
        return -ERROR_BUFFER_SIZE;

    for(int pix = 0; pix < size; pix++) {
        if(new[pix] - prev[pix] > FRAME_DIFF_THRESHOLD) {
            diff_count++;
        }
    }
    syslog(LOG_INFO, "Successfully calculated diff");

    return diff_count;
}

int differencing(cbuff_struct_t *frame_buffer) {
    int frame_count_limit = FRAMES_TO_SERVICE;
    long int temp;
    if(garbage_frames==0) {
        if(first_capture) {
            //read_frame(frame_buffer, READ_DIFF_POINTER, previous_frame, &size);                               // set as previous frame
            previous_frame = read_frame_ptr(frame_buffer, READ_DIFF_POINTER, &size);
            read_timestamp(frame_buffer, READ_DIFF_POINTER, &temp_time);
            //printf("**entry in diff=%p\n", previous_frame);
            old_ts = getMSfromTimestamp(&temp_time);
            if(nextPtr(READ_DIFF_POINTER) == false)
                return -ERROR_NEXT_PTR;
            frame_count_limit--;  
            first_capture = false;
        } else {
            while((nextPtr(READ_DIFF_POINTER) != false) && (frame_count_limit > 0)) {
                //circular_buff_lock();
                    //read_frame(frame_buffer, READ_DIFF_POINTER, new_frame, &size);                                    // fetch new frame
                    new_frame = read_frame_ptr(frame_buffer, READ_DIFF_POINTER, &size);
                    // printf("entry in diff=%p\n", new_frame);
                    // printf("size got=%d \n", size); 
                    temp = perform_diff(new_frame, previous_frame, size);
                    //printf("Diff=%d \n", temp); 
                    if(temp < PIXEL_DIFFERENCE_THRESHOLD) {    // perform difference
                        write_usefulness(frame_buffer, temp);                                                 // marking // FRAME_USEFUL
                        //print_cbuf_info();
                        syslog(LOG_INFO, "Differencing: pointer %d marked as useful", rptr_diff);
                    } else {
                        write_usefulness(frame_buffer, FRAME_NOT_USEFUL);
                    }
                    previous_frame = new_frame;
                //circular_buff_unlock();
                frame_count_limit--;
            }
        }
    }
    return (FRAMES_TO_SERVICE - frame_count_limit);
}

int frame_select(cbuff_struct_t *frame_buffer) {
    int ret = -1;
    int temp = 0;
    int temp_diff;
    cbuff_struct_t *element;

    if(first_capture == false) {
        ret = 0;
        while((nextPtr(READ_SEL_POINTER) != false) && 
              (frame_count <=  FRAME_CAPTURE_COUNT)) {
            read_timestamp(frame_buffer, READ_SEL_POINTER, &temp_time);
            new_ts = getMSfromTimestamp(&temp_time);
            if((new_ts > old_ts) && ((new_ts - old_ts) > (FRAME_SELECTION_TIME_MS - offset))) {
                temp_diff = read_usefulness(frame_buffer, READ_SEL_POINTER);
                if(temp_diff > -1) {

                    circular_buff_lock();
                        write_framecount(frame_buffer, frame_count);                           //write frame count
                        element = read_cbuf_entry(frame_buffer);
                        //printf("Frame select: size=%d framecount=%d time=%d\n", element->size, element->frame_count, element->timestamp.tv_sec);
                        temp = push_frame_fifo(element);                          // push pointer to queue
                    circular_buff_unlock();
                
                    if(temp == -1)
                        printf("Queue full. Unable to push to queue\n");
                    if(temp == 0)
                        printf("Frame %d successsfully pushed to queue, diff=%d, time=%d ", frame_count, temp_diff, new_ts);
                        print_cbuf_info();
                    frame_count++;
                    ret = 1;
                    old_ts = new_ts;
                    break;
                }
            }
        }
    }

    return ret;   
}

unsigned int getFrameCount(void) {
    return frame_count;
}