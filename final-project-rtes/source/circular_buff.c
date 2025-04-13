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
#include "pthread.h"
#include "../includes/circular_buff.h"


// Declare memory for the queue/buffer, and our write and read pointers.
//static cbuff_struct_t *frame_buffer; // the queue
int  wptr      = 0;                  // write pointer
int  rptr_diff = 0;                  // read pointer for differencing
int  rptr_sel  = 0;                  // read pointer for selection
int  depth = 0;                      // depth variable

pthread_mutex_t sgl;

// void init_circular_buffer(cbuff_struct_t *global_buff) {
//   frame_buffer = global_buff;
// }

bool nextPtr(pointer_type_t type) {
  bool ret = true;

  if(type == WRITE_POINTER) {
    if(wptr == (QUEUE_DEPTH - ONE)) {
      wptr = ZERO;
    } else {  
      wptr++;
    }
    if(depth < QUEUE_DEPTH)
      depth++;
  } else if(type == READ_DIFF_POINTER) {
    if(((rptr_diff < (wptr - 1)) && (QUEUE_DEPTH != depth)) ||
        (rptr_diff > wptr)       && (wptr != 0)) {              // ((rptr_diff == (QUEUE_DEPTH - 1)) && ((wptr - 1) > 0))
        if(rptr_diff == (QUEUE_DEPTH - ONE)) {
          rptr_diff = ZERO;
        } else {  
          rptr_diff++;
        }
    } else {
      ret = false;
    }
  } else if(type == READ_SEL_POINTER) {
    if(((rptr_sel < (rptr_diff - 1)) && (QUEUE_DEPTH != depth)) ||
        (rptr_sel > rptr_diff)       && (rptr_diff != 0)) {       // ((rptr_sel == (QUEUE_DEPTH - 1)) && ((rptr_diff - 1) > 0))
        if(rptr_sel == (QUEUE_DEPTH - ONE)) {
          rptr_sel = ZERO;
        } else {  
          rptr_sel++;
        }
        if(depth > 0)
          depth--;
    } else {
      ret = false;
    }
  } else {
    ret = false;
  }

  return ret;
} // nextPtr()

void reset_queue (void) {
  // reset depth
  depth = ZERO;

  // reset wptr
  wptr = ZERO;
  
  // reset rptr
  rptr_diff = ZERO;
  rptr_sel = ZERO;
  
} // reset_queue()

bool write_size_and_time(cbuff_struct_t *frame_buffer, int size, struct timespec *timestamp) {
  bool ret = false;

  // write to queue
  frame_buffer[wptr].timestamp.tv_sec = timestamp->tv_sec;
  frame_buffer[wptr].timestamp.tv_nsec = timestamp->tv_nsec;
  frame_buffer[wptr].size = size;
  frame_buffer[wptr].usefulness = -20;
  nextPtr(WRITE_POINTER);
  ret = true;

  return ret;
} // write_frame() 

bool write_usefulness(cbuff_struct_t *frame_buffer, int usefulness) {
  bool ret = true;

  // write to queue
  frame_buffer[rptr_diff].usefulness = usefulness;
  nextPtr(READ_DIFF_POINTER);

  return ret;
} // write_usefulness() 

bool write_framecount(cbuff_struct_t *frame_buffer, int framecount) {
  bool ret = true;

  // write to queue
  frame_buffer[rptr_sel].frame_count = framecount;

  return ret;
} // write_usefulness() 

int read_usefulness(cbuff_struct_t *frame_buffer, pointer_type_t type) {
  int ret = ERROR_READ_UFN;
  bool valid_args = false; 
  int index = 0;

  if((type > START_OF_ENUM_PTR_TYPE) && (type < END_OF_ENUM_PTR_TYPE))
    valid_args = true;
  
  if((valid_args) && (depth != ZERO)) {
    if(type == READ_DIFF_POINTER)
      index = rptr_diff;
    if(type == READ_SEL_POINTER)
      index = rptr_sel;
    // read ops    
    ret = frame_buffer[index].usefulness;
  }

  return ret;
} // read_usefulness()

int read_timestamp(cbuff_struct_t *frame_buffer, pointer_type_t type, struct timespec *time) {
  int ret = ERROR_READ_UFN;
  bool valid_args = false; 
  int index = 0;

  if((type > START_OF_ENUM_PTR_TYPE) && (type < END_OF_ENUM_PTR_TYPE))    valid_args = true;
  
  if((valid_args) && (depth != ZERO)) {
    if(type == READ_DIFF_POINTER)
      index = rptr_diff;
    if(type == READ_SEL_POINTER)
      index = rptr_sel;
    
    // read ops
    time->tv_sec = frame_buffer[index].timestamp.tv_sec;
    time->tv_nsec = frame_buffer[index].timestamp.tv_nsec;

    ret = 0;
  }
  
  return ret;
} // read_timestamp()  

bool read_frame(cbuff_struct_t *frame_buffer, pointer_type_t type, unsigned char *local_buff, int *size) {
  bool ret = false;
  bool valid_args = false; 
  int index = 0;

  //if((local_buff != NULL) && (size != NULL))
    if((type > START_OF_ENUM_PTR_TYPE) && (type < END_OF_ENUM_PTR_TYPE)) 
      valid_args = true;
  
  if((valid_args) && (depth != ZERO)) {
    if(type == READ_DIFF_POINTER)
      index = rptr_diff;
    if(type == READ_SEL_POINTER)
      index = rptr_sel;

    // read ops
    printf("entry before=%p\n", frame_buffer[index].buffer);
    local_buff = frame_buffer[index].buffer;
    printf("entry after=%p\n", local_buff);
    *size = frame_buffer[index].size;
    ret = true;
  }
  
  return ret;
} // read_frame() 

unsigned char *read_frame_ptr(cbuff_struct_t *frame_buffer, pointer_type_t type, int *size) {
  unsigned char *ret = NULL;
  bool valid_args = false; 
  int index = 0;

  //if((local_buff != NULL) && (size != NULL))
    if((type > START_OF_ENUM_PTR_TYPE) && (type < END_OF_ENUM_PTR_TYPE)) 
      valid_args = true;
  
  if((valid_args) && (depth != ZERO)) {
    if(type == READ_DIFF_POINTER)
      index = rptr_diff;
    if(type == READ_SEL_POINTER)
      index = rptr_sel;

    // read ops
    ret = frame_buffer[index].buffer;
    *size = frame_buffer[index].size;
  }
  
  return ret;
} // read_frame() 

cbuff_struct_t *read_cbuf_entry(cbuff_struct_t *frame_buffer) {
  return &(frame_buffer[rptr_sel]);
}

bool circular_buff_lock(void) {
  bool ret = false;
  if(pthread_mutex_lock(&sgl) == 0)
    ret = true;

  return ret;
} 

bool circular_buff_unlock(void) {
  bool ret = false;
  if(pthread_mutex_unlock(&sgl) == 0)
    ret = true;

  return ret;
} 

cbuff_struct_t *get_wptr(cbuff_struct_t *frame_buffer) {
  return &(frame_buffer[wptr]);
}

int getMSfromTimestamp(struct timespec *time) {
  return ((((double)(time->tv_sec)) * MSEC_PER_SEC + ((double)(time->tv_nsec) / NANOSEC_PER_MSEC)));
}

void print_cbuf_info(void) {
  printf("wptr:%d, rptr_diff:%d, rptr_sel:%d, depth:%d \n",wptr,rptr_diff,rptr_sel,depth);
}