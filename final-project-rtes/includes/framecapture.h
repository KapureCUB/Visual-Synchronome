/**
*  
* This header contains the helper functions for capturing frames
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

#ifndef FRAMECAPTURE_H
#define FRAMECAPTURE_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>            
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <linux/videodev2.h>
#include <time.h>
// for logging
#include <syslog.h>

#include "../includes/circular_buff.h"

#define CLEAR(x) memset(&(x), 0, sizeof(x))

// camera conversion macros
#define HRES 640
#define VRES 480
#define HRES_STR "640"
#define VRES_STR "480"
#define DEFAULT_VIDEO_DEVICE      "/dev/video0"

#define FRAME_RATE_SET        (60)
#define YUV_TO_RGB_FACTOR     (6/4)

// capture buffer defination
struct buffer {
    void   *start;
    size_t  length;
};

/**
 * @brief Function to initialize video device using V4L2 driver
 * @param fd - file descriptor for video device
 * @param dev_name - device name to init
 * @return no return
 */
void init_device(const int fd, const char *dev_name);

/**
 * @brief Function to close video device using V4L2 driver
 * @param fd - file descriptor for video device
 * @return ret - 0-success, else failure
 */
int close_device(const int fd);

/**
 * @brief Function to open video device file using V4L2 driver
 * @param dev_name - device name to init
 * @return fd - returns the file descriptor for video device
 */
int open_device(const char *dev_name); 

/**
 * @brief Function to stop capturing frames. Sets the video stream off
 * @param fd - file descriptor for video device
 * @return no return
 */
void stop_capturing(const int fd);

/**
 * @brief Function to start capturing frames. Queues buffer to capture
 * frames.
 * @param fd - file descriptor for video device
 * @return no return
 */
void start_capturing(const int fd);

/**
 * @brief Function to unmap the memory mapped region in the 
 * processor address space and frrees the allocated buffers
 * @return no return
 */
void uninit_device(void);

/**
 * @brief Function to read the captured frames. Dequeues the buffer 
 * and returns it
 * @param fd - file descriptor for video device
 * @return no return
 */
void read_frames(const int fd, cbuff_struct_t *frame_buffer);


#ifdef	__cplusplus
}
#endif

#endif