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

#ifndef CIRCULAR_BUFF_H   
#define CIRCULAR_BUFF_H


// These are included here so that data types used below, and therefore are
// needed by the caller, are defined and available.
#include <stdint.h>    // for uint8_t etc.
#include <stdbool.h>   // for bool
#include <sys/time.h>
#include <time.h>


// This is the number of entries in the queue. Please leave
// this value set to 16.
#define QUEUE_DEPTH      (90)            // 110MB buffer space

#define USE_ALL_ENTRIES  (1)

#define MAX_BUFFER_LENGTH  (1280*960)

#define ZERO               (0)
#define ONE                (1)

// Errors
#define ERROR_READ_UFN     (2)            // usefulness
#define ERROR_BUFFER_SIZE  (3)
#define ERROR_NEXT_PTR     (4)

// timekeeping macros
#define MY_CLOCK                CLOCK_MONOTONIC_RAW
#define USEC_PER_MSEC           (1000.0)
#define NANOSEC_PER_MSEC        (1000000.0)
#define NANOSEC_PER_SEC         (1000000000.0)
#define MSEC_PER_SEC            (1000.0)

// Pointer types
typedef enum { 
  START_OF_ENUM_PTR_TYPE,
  WRITE_POINTER,
  READ_DIFF_POINTER,
  READ_SEL_POINTER,
  END_OF_ENUM_PTR_TYPE  
}pointer_type_t;


typedef struct {
  struct timespec timestamp;                 // timestamp in milliseconds for the acquired frame
  int usefulness;                            // usefulness of the frame. 1=useful, -1=not useful, 0=not marked 
  int size;                                  // size of the buffer     
  unsigned int frame_count;                  // frame count for the frame data                   
  unsigned char buffer[MAX_BUFFER_LENGTH];   // buffer for storing frame data
}cbuff_struct_t;

bool nextPtr(pointer_type_t type);
void reset_queue(void);
bool write_size_and_time(cbuff_struct_t *frame_buffer, int size, struct timespec *timestamp); 
bool write_usefulness(cbuff_struct_t *frame_buffer, int usefulness);
int  read_usefulness(cbuff_struct_t *frame_buffer, pointer_type_t type);
int  read_timestamp(cbuff_struct_t *frame_buffer, pointer_type_t type, struct timespec *time);
bool read_frame(cbuff_struct_t *frame_buffer, pointer_type_t type, unsigned char *local_buff, int *size);
bool circular_buff_lock(void);
bool circular_buff_unlock(void);
cbuff_struct_t *get_wptr(cbuff_struct_t *frame_buffer);
int getMSfromTimestamp(struct timespec *time);
bool write_framecount(cbuff_struct_t *frame_buffer, int framecount);
cbuff_struct_t *read_cbuf_entry(cbuff_struct_t *frame_buffer);
void print_cbuf_info(void);
//void init_circular_buffer(cbuff_struct_t *global_buff);
unsigned char *read_frame_ptr(cbuff_struct_t *frame_buffer, pointer_type_t type, int *size);

#endif // __MY_CIRCULAR_BUFFER__

