/*
* MIT License
Copyright (c) 2021 - current
Authors: Animesh Trivedi
This code is part of the Storage System Course at VU Amsterdam
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef STOSYS_PROJECT_ZNS_DEVICE_H
#define STOSYS_PROJECT_ZNS_DEVICE_H

#include <cstdint>
#include <pthread.h>

extern "C"{
//https://github.com/mplulu/google-breakpad/issues/481 - taken from here
#define typeof _typeof_
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

/* after a successful initialization of a device, you must set these ZNS device parameters for testing */
struct zns_device_testing_params {
    // LBA size at the ZNS device
    uint32_t zns_lba_size;
    // Zone size at the ZNS device
    uint32_t zns_zone_capacity;
    // total number of zones
    uint32_t zns_num_zones;
};

struct user_zns_device {
    /* these are user visible properties */
    uint32_t lba_size_bytes; // the user device LBA size - should be some multiple of the ZNS device page size, you can keep it as it is
    uint64_t capacity_bytes; // total user device capacity
    struct zns_device_testing_params tparams; // report back some ZNS device-level properties to the user (for testing only, this is not needed for functions
    // your own private data
    void *_private;
};

struct zns_device_metadata
{
    // file descriptor of the opened device
    int fd;
    // namespace id of the target namespace
    uint32_t nsid;

    uint64_t log_zone_slba;

    // Let's add mdts for later usage
    uint32_t mdts;
    
    // garbage collection watermark (i.e. clean zones to keep, typically =1 in the test script)
    uint32_t gc_watermark;
    
    // start and end of the log zone and the data zone
    // for m4 perhaps
    uint32_t log_zone_start, log_zone_end;
    uint32_t data_zone_start, data_zone_end;
    uint32_t n_log_zone;
    
    uint32_t n_blocks_per_zone;

    uint8_t *zone_states;

    int log_zone_num_config;

    pthread_mutex_t gc_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t start_gc = PTHREAD_COND_INITIALIZER;
    pthread_cond_t stop_gc = PTHREAD_COND_INITIALIZER;

    pthread_t gc_thread_id = 0;
    bool gc_thread_stop = false;
    bool trigger_my_gc = false;
    // ...
};

/* device initialization parameters 
* name: is the name of the device on which this FTL should run, so your /dev/xxx device.
* log_zone: is the number of zones your FTL should use to implement a Log. The remaining 
* are for the Data zone. The default value is 3. We will test with various possible 
* values between 3-X (X is determined by the number of total zones available on a 
* device). 
* gc_wmark: is the GC watermark, the number of minimum free zones that the GC must maintain. 
* You are free to start GC before you get to this value, but at this value GC 
* must be running and cleaning zones. The default value is 1. So when the last 
* zone is left free (out of the 3), clean up some space from the Log by converting 
* some mapping from Log to Data.
* force_reset: If true, then always reset the whole device before using. This is the default behavior. 
* Changing this come in handy for M5 when using persistency. You do not have to 
* touch this variable for M2-M3, but implement this behavior to reset the whole device. 
* Setup: 
* 0 -------------------------------------------------- total_device_zones |
* <----- log_zones -------------> <---------- data_zones ------------------------>
* 
* ^^^ copied-data from the log to convert it into block-mapped 
* new writes go here page-mapped 
*
* The data zone size would be the capacity exposed to the user for read/write. The log space is used internally by your FTL. With the default values: 
*/
struct zdev_init_params{
    char *name;
    int log_zones;
    int gc_wmark;
    bool force_reset;
};

int init_ss_zns_device(struct zdev_init_params *params, struct user_zns_device **my_dev);
int zns_udevice_read(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
int zns_udevice_write(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
int deinit_ss_zns_device(struct user_zns_device *my_dev);
};

#endif //STOSYS_PROJECT_ZNS_DEVICE_H