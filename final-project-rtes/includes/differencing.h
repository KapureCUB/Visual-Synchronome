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
#ifndef DIFFERENCING_H   
#define DIFFERENCING_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>    // for uint8_t etc.
#include <stdbool.h>   // for bool
#include "../includes/circular_buff.h"

//#define FRAME_SELECTION_RATE_HZ      (10.0)
//#define FRAME_SELECTION_RATE_HZ      (1.0)
#define FRAME_SELECTION_RATE_HZ      (10.0)
#define FRAME_SELECTION_TIME_MS      (1000.0/FRAME_SELECTION_RATE_HZ)
#define FRAME_CAPTURE_COUNT          (180)

int differencing(cbuff_struct_t *frame_buffer);
int frame_select(cbuff_struct_t *frame_buffer);
unsigned int getFrameCount(void);

#ifdef	__cplusplus
}
#endif

#endif