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

#ifndef WRITEBACK_H   
#define WRITEBACK_H

#ifdef	__cplusplus
extern "C" {
#endif

// These are included here so that data types used below, and therefore are
// needed by the caller, are defined and available.
#include <stdint.h>    // for uint8_t etc.
#include <stdbool.h>   // for bool

#include "../includes/circular_buff.h"

#define MAX_FIFO_DEPTH     (10)

int push_frame_fifo(cbuff_struct_t *element);
int writeback(void);
void init_fifoQ(void);


#ifdef	__cplusplus
}
#endif

#endif //WRITEBACK_H