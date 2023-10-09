/*
 * MIT License
Copyright (c) 2021 - current
Authors:  Animesh Trivedi
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
#include <libnvme.h>
#include <map>
#include <vector>

extern "C"
{
// https://github.com/mplulu/google-breakpad/issues/481 - taken from here
#define typeof typeof
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) ); })

    /* after a successful initialization of a device, you must set these ZNS device parameters for testing */
    struct zns_device_testing_params
    {
        uint32_t zns_lba_size;
        uint32_t zns_zone_capacity;
        uint32_t zns_num_zones;
    };

    struct user_zns_device
    {
        /* these are user visible properties */
        uint32_t lba_size_bytes;                  // LBA size
        uint64_t capacity_bytes;                  // total cpacity of the device in bytes (this would be the size of the data zones (excluding the log zones))
        struct zns_device_testing_params tparams; // a few ZNS internal parameter used for testing
        // device's own private pointer, not usable by the user
        void *_private;
    };

    struct ThreadData {
    struct user_zns_device *my_dev;
};

    /* device initialization parameters
     * name: is the name of the device on which this FTL should run, so your /dev/xxx device.
     * log_zone: is the number of zones your FTL should use to implement a Log. The remaining
     *           are for the Data zone. The default value is 3. We will test with various possible
     *           values between 3-X (X is determined by the number of total zones available on a
     *           device).
     * gc_wmark: is the GC watermark, the number of minimum free zones that the GC must maintain.
     *           You are free to start GC before you get to this value, but at this value GC
     *           must be running and cleaning zones. The default value is 1. So when the last
     *           zone is left free (out of the 3), clean up some space from the Log by converting
     *           some mapping from Log to Data.
     * force_reset: If true, then always reset the whole device before using. This is the default behavior.
     *               Changing this come in handy for M5 when using persistency. You do not have to
     *               touch this variable for M2-M3, but implement this behavior to reset the whole device.
     * Setup:
     * 0 -------------------------------------------------- total_device_zones |
     * <----- log_zones -------------> <---------- data_zones ------------------------>
     *
     *      ^^^                        copied-data from the log to convert it into block-mapped
     * new writes go here page-mapped
     *
     * The data zone size would be the capacity exposed to the user for read/write. The log space is used internally by your FTL. With the default values:
     */
    struct zdev_init_params
    {
        char *name;
        int log_zones;
        int gc_wmark;
        bool force_reset;
    };

    struct dev_props
    {
        uint64_t fd, current_log_zone, current_data_zone, lba_size, nlb, num_zones, number_of_log_zones;
        uint64_t current_write_pointer;
        uint32_t nsid;
        int gc_wmark;
        struct nvme_id_ns *s_nsid;
        struct nvme_zns_id_ns *zns_id_ns;
        struct nvme_zns_id_ctrl *zns_id_ctrl;
        std::map<uint64_t, uint64_t> log_mapping;
        std::map<uint64_t, uint64_t> data_zone_mapping;
        bool* is_zone_empty_array;
    };

    int init_ss_zns_device(struct zdev_init_params *, struct user_zns_device **my_dev);
    int zns_udevice_read(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
    int zns_udevice_write(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
    int deinit_ss_zns_device(struct user_zns_device *my_dev);
    int write_io(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
    int read_io(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size);
    int reset_whole_zone(int fd, uint32_t nsid, uint64_t slba);
};

#endif // STOSYS_PROJECT_ZNS_DEVICE_H