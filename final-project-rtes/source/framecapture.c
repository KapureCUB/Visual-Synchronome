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

#include "../includes/framecapture.h"
#include "../includes/circular_buff.h"   

#include "../includes/sequencer.h"

// variables declaration 
static struct v4l2_format fmt;                            // V4L2 struct

extern double start_realtime;

struct buffer          *buffers;
static unsigned int     n_buffers;        
int garbage_frames = 20;                                
//unsigned char bigbuffer[(1280*960)];                      // buffer for RGB conversion 

/**
 * @brief Function to invoke IOCTL system call for V4L2 driver
 * @param fh - file handle
 * @param request - read or write request
 * @param arg - buffer pointer 
 * @return r - return value of ioctl system call
 */
static int xioctl(int fh, int request, void *arg) {
    int r;
    do {
        r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

/**
 * @brief Function to be called when an exception is triggered
 * @param s - error type
 * @return no return
 */
static void errno_exit(const char *s) {
    fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

/**
 * @brief Helper function to convert YUV to RGB.
 * @param y - Y val in YUV
 * @param u - U val in YUV
 * @param v - V val in YUV
 * @param r - output R in RGB
 * @param g - output G in RGB
 * @param b - output B in RGB
 * @return no return
 */
static void yuv2rgb(int y, int u, int v, unsigned char *r, unsigned char *g, unsigned char *b)
{
   int r1, g1, b1;

   // replaces floating point coefficients
   int c = y-16, d = u - 128, e = v - 128;       

   // Conversion that avoids floating point
   r1 = (298 * c           + 409 * e + 128) >> 8;
   g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
   b1 = (298 * c + 516 * d           + 128) >> 8;

   // Computed values may need clipping.
   if (r1 > 255) r1 = 255;
   if (g1 > 255) g1 = 255;
   if (b1 > 255) b1 = 255;

   if (r1 < 0) r1 = 0;
   if (g1 < 0) g1 = 0;
   if (b1 < 0) b1 = 0;

   *r = r1 ;
   *g = g1 ;
   *b = b1 ;
}


/**
 * @brief Function to convert YUV to RGB. Stores the output in @var bigbuffer
 * @param p - input frame buffer in YUV format
 * @param size - size of the YUV buffer
 * @return no return
 */
static void process_image(const void *p, int size, unsigned char *bigbuffer) {
    int i, newi=0;
    int y_temp, y2_temp, u_temp, v_temp;
    unsigned char *pptr = (unsigned char *)p;
    
    for(i=0, newi=0; i<size; i=i+4, newi=newi+6) {
        y_temp=(int)pptr[i]; 
        u_temp=(int)pptr[i+1]; 
        y2_temp=(int)pptr[i+2]; 
        v_temp=(int)pptr[i+3];
        yuv2rgb(y_temp, u_temp, v_temp, &bigbuffer[newi], &bigbuffer[newi+1], 
                                        &bigbuffer[newi+2]);
        yuv2rgb(y2_temp, u_temp, v_temp, &bigbuffer[newi+3], &bigbuffer[newi+4],
                                         &bigbuffer[newi+5]);
    }
}

/**
 * @brief Function to initialize the memory map for frame capture
 * @param fd - file descriptor for video device
 * @param dev_name - device name to init
 * @return no return
 */
static void init_mmap(const int fd, const char *dev_name) {
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count  = 6;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s does not support memory mapping\n", dev_name);
            syslog(LOG_INFO, "%s does not support memory mapping\n", dev_name);
            exit(EXIT_FAILURE);
        } else {
            errno_exit("VIDIOC_REQBUFS");
        }
    }
    if (req.count < 2) {
        fprintf(stderr, "Insufficient buffer memory on %s\n", dev_name);
        syslog(LOG_INFO, "Insufficient buffer memory on %s\n", dev_name);
        exit(EXIT_FAILURE);
    }

    buffers = calloc(req.count, sizeof(*buffers));

    if (!buffers) {
        fprintf(stderr, "Out of memory\n");
        syslog(LOG_INFO, "Out of memory\n");
        exit(EXIT_FAILURE);
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = n_buffers;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf)) {
            syslog(LOG_INFO, "Error in VIDIOC_QUERYBUF\n");
            errno_exit("VIDIOC_QUERYBUF");
        }

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start = mmap(NULL,                      //start anywhere 
                                        buf.length,                //requested length
                                        PROT_READ | PROT_WRITE,    //required 
                                        MAP_SHARED,                //recommended
                                        fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start) {
            syslog(LOG_INFO, "Error in mmap");
            errno_exit("mmap");
        }
    }
    syslog(LOG_INFO,"Memory mapping successful");
}

/**
 * @brief Function to initialize video device using V4L2 driver
 * @param fd - file descriptor for video device
 * @param dev_name - device name to init
 * @return no return
 */
void init_device(const int fd, const char *dev_name) {
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    unsigned int min;
    
    // verify the device capabilities
    if(-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
        if (EINVAL == errno) {
            fprintf(stderr, "%s is no V4L2 device\n", dev_name);
            exit(EXIT_FAILURE);
        } else {
                errno_exit("VIDIOC_QUERYCAP");
        }
    }
    if(!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "%s is no video capture device\n", dev_name);
        exit(EXIT_FAILURE);
    }
    if(!(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "%s does not support streaming i/o\n", dev_name);
        exit(EXIT_FAILURE);
    }

    // select video input, video standard and tune
    CLEAR(cropcap);
    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect;                               //reset to default 

        if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
            if(errno == EINVAL) {
                /* Cropping not supported. */
                syslog(LOG_INFO,"Cropping not supported\n");
            }
        }
    }
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    syslog(LOG_INFO, "FORCING FORMAT\n");
    fmt.fmt.pix.width       = HRES;
    fmt.fmt.pix.height      = VRES;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;               //default coding format for C270
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
            errno_exit("VIDIOC_S_FMT");

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
            fmt.fmt.pix.bytesperline = min;
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
            fmt.fmt.pix.sizeimage = min;

    init_mmap(fd, dev_name);
}

/**
 * @brief Function to close video device using V4L2 driver
 * @param fd - file descriptor for video device
 * @return ret - 0-success, else failure
 */
int close_device(const int fd) {
    int ret = 0;
    if (-1 == close(fd)) {
            errno_exit("close");
        ret = -1;
    }
    return ret;
}

/**
 * @brief Function to open video device file using V4L2 driver
 * @param dev_name - device name to init
 * @return fd - returns the file descriptor for video device
 */
int open_device(const char *dev_name) {
    int fd;
    struct stat st;

    if(-1 == stat(dev_name, &st)) {
        fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                   dev_name, errno, strerror(errno));
        syslog(LOG_INFO, "Cannot identify '%s': %d, %s\n",
                    dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    if(!S_ISCHR(st.st_mode)) {
        fprintf(stderr, "%s is no device\n", dev_name);
        syslog(LOG_INFO,"%s is no device\n", dev_name);
        exit(EXIT_FAILURE);
    }

    // open the camera device and store file descriptor in fd
    fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
    if(-1 == fd) {
        fprintf(stderr, "Cannot open '%s': %d, %s\n",
                    dev_name, errno, strerror(errno));
        syslog(LOG_INFO,"Cannot open '%s': %d, %s\n",
                    dev_name, errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    syslog(LOG_INFO,"Video device opened successfully");
    return fd;
}

/**
 * @brief Function to stop capturing frames. Sets the video stream off
 * @param fd - file descriptor for video device
 * @return no return
 */
void stop_capturing(const int fd) {
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type)) {
        errno_exit("VIDIOC_STREAMOFF");
        syslog(LOG_INFO,"Error in stoping capture - VIDIOC_STREAMOFF\n");
    }      
}

/**
 * @brief Function to start capturing frames. Queues buffer to capture
 * frames.
 * @param fd - file descriptor for video device
 * @return no return
 */
void start_capturing(const int fd) {
    unsigned int i;
    enum v4l2_buf_type type;

    syslog(LOG_INFO,"Starting capture. Allocating buffers");

    for (i = 0; i < n_buffers; ++i) {
        syslog(LOG_INFO, "allocated buffer %d\n", i);
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf)) {
            syslog(LOG_INFO,"Error in VIDIOC_QBUF\n");
            errno_exit("VIDIOC_QBUF");
        }
    }
    syslog(LOG_INFO,"Buffer allocation successful");

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type)) {
        syslog(LOG_INFO,"Error in VIDIOC_STREAMON");
        errno_exit("VIDIOC_STREAMON");
    }
    syslog(LOG_INFO,"Video streaming enabled");
}

/**
 * @brief Function to unmap the memory mapped region in the 
 * processor address space and frrees the allocated buffers
 * @return no return
 */
void uninit_device(void) {
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
        if (-1 == munmap(buffers[i].start, buffers[i].length)) {
            errno_exit("munmap");
            syslog(LOG_INFO,"Error in munmap\n");
        }
    free(buffers);
}

/**
 * @brief Function to read the captured frames. Dequeues the buffer 
 * and returns it
 * @param fd - file descriptor for video device
 * @param *buf - buffer pointer to save frame captured
 * @return no return
 */
void read_frames(const int fd, cbuff_struct_t *frame_buffer) {
    int r;
    fd_set fds;
    struct v4l2_buffer dbuf;
    cbuff_struct_t *buffer_entry;
    struct timespec frame_time;
    struct timespec current_time_val;
    struct timeval tv = {                                     // timeout val for select
        .tv_sec   = 2,
        .tv_usec  = 0
    };

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    dbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dbuf.memory = V4L2_MEMORY_MMAP;

    // check if fd is ready to read new frame value
    r = select(fd + 1, &fds, NULL, NULL, &tv);
    while(r <= 0) {
        if (-1 == r) {
            if (EINTR == errno) {
                r = select(fd + 1, &fds, NULL, NULL, &tv);
                continue;
            }
            syslog(LOG_INFO, "Error in select syscall during capture");
            errno_exit("select");
        }
        if(r == 0) {
            fprintf(stderr, "select timeout\n");
            syslog(LOG_INFO, "Select syscall timeout");
            clock_gettime(MY_CLOCK, &current_time_val);
            syslog(LOG_INFO, "select exit called @ sec=%6.9lf\n", realtime(&current_time_val)-start_realtime);
            exit(EXIT_FAILURE);
        }
    }
    syslog(LOG_INFO, "Select syscall returned %d", r);
    // capture frame acqisition time
    clock_gettime(MY_CLOCK, &frame_time);                     // set start time

    if(-1 == xioctl(fd, VIDIOC_DQBUF, &dbuf)) {
        switch (errno) {
            case EAGAIN:
                syslog(LOG_INFO, "Please try reading again");
                return;
            default:
                syslog(LOG_INFO, "Unable to dequeue buffer. VIDIOC_DQBUF\n");
                errno_exit("VIDIOC_DQBUF");
        }
    }
    assert(dbuf.index < n_buffers);
    
    if(garbage_frames == 0) {
        circular_buff_lock();

        buffer_entry = get_wptr(frame_buffer);
        process_image(buffers[dbuf.index].start, dbuf.bytesused, buffer_entry->buffer);       // convert to RGB
        //memcpy(ibuff, bigbuffer, sizeof(bigbuffer));                                        // transfer to o/p buffer
        //CLEAR(bigbuffer);
        write_size_and_time(frame_buffer, ((dbuf.bytesused * 6) / 4), &frame_time);                         // set the size for dumping and time
        circular_buff_unlock();
    }

    // queue the buffer 
    if (-1 == xioctl(fd, VIDIOC_QBUF, &dbuf)) {
        syslog(LOG_INFO, "Unable to queue buffer. VIDIOC_QBUF\n");
        errno_exit("VIDIOC_QBUF");
    }
    
    if(garbage_frames>0)
        garbage_frames--;
    syslog(LOG_INFO, "Frame read successfully");
}
