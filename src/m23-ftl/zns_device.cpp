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

#include <cerrno>
#include <cstdint>

// User imported
#include <libnvme.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <unordered_map>
#include <iostream>

#include "zns_device.h"
#include "../common/unused.h"

extern "C" {

    std::unordered_map<int64_t, int64_t> log_zone_mapping;
    std::unordered_map<int64_t, int64_t> data_zone_mapping;

    struct user_zns_device *zns_device;
    struct zns_device_metadata *zns_metadata;

    const int EMPTY_ZONE = 1;
    const int FULL_ZONE = 14;

    /*
    The functions mmap_registers() and get_mdts_size() are intended to extract MDTS value of ZNS device.
    Comment them out if hardcoding a constant yields better performance, and if test environment will remain the same.
    */

    static void *mmap_registers(int fd) {
        int ret = -ENOSYS;
        // Here goes the dirty hack
        nvme_root_t r;
        r = nvme_scan(nullptr);
        nvme_ctrl_t c = nullptr;
        nvme_ns_t n = nullptr;

        std::string fd_path = "/proc/self/fd/" + std::to_string(fd);
        char buf[512];
        ret = readlink(fd_path.data(), buf, sizeof(buf));
        if (ret < 0) {
            printf("[ERROR] ERROR LOOKING UP LINK, CHECK YOUR FILE DESC");
        }
        std::string device_name = std::string(buf).substr(5);

        c = nvme_scan_ctrl(r, device_name.data());

        char path[512];
        if (c) {
            snprintf(path, sizeof(path), "%s/device/resource0",
            nvme_ctrl_get_sysfs_dir(c));
            nvme_free_ctrl(c);
        } else {
            n = nvme_scan_namespace(device_name.data());
            if (!n) {
                fprintf(stderr, "Unable to find %s\n", device_name.data());
                return NULL;
            }
            snprintf(path, sizeof(path), "%s/device/device/resource0",
            nvme_ns_get_sysfs_dir(n));
            nvme_free_ns(n);
        }

        void *membase;
        int device_resource_fd = open(path, O_RDONLY);
        membase = mmap(nullptr, getpagesize(), PROT_READ, MAP_SHARED, device_resource_fd, 0);
        if (membase == MAP_FAILED) {
            fprintf(stderr, "[ERROR] FAILED TO MAP THE MEMBASE");
            membase = nullptr;
        }
        close(device_resource_fd);
        return membase;
    }

    struct nvme_bar_cap {
    __u16 mqes;
    __u8 ams_cqr;
    __u8 to;
    __u16 bps_css_nssrs_dstrd;
    __u8 mpsmax_mpsmin;
    __u8 rsvd_cmbs_pmrs;
    };

    uint64_t get_mdts_size(int fd, ...) {
        int ret = -ENOSYS;
        struct nvme_id_ctrl ctrl{};
        ret = nvme_identify_ctrl(fd, &ctrl);
        if (ret != 0) {
            printf("[ERROR] Failed to identify device");
        }

        void *membase = mmap_registers(fd);

        const volatile __u32 *p = (__u32*)(membase);
        __u32 low, high;

        low = le32_to_cpu(*p);
        high = le32_to_cpu(*(p + 1));

        uint64_t cap = ((__u64) high << 32) | low;
        auto* cap_struct = (struct nvme_bar_cap *)&cap;
        int mps_min = 1 << (12 + (cap_struct->mpsmax_mpsmin & 0x0f));

        return pow(2, ctrl.mdts - 1) * mps_min;
    }

    
    int io_with_mdts(int fd, uint32_t nsid, uint64_t slba, void *buffer, uint64_t buf_size, bool read) { 
        int ret = -ENOSYS;

        uint64_t size_to_io, buf_ptr, single_io_size, write_pointer, lba_num, mdts_size = zns_metadata->mdts, lba_size = zns_device->lba_size_bytes;
        size_to_io = buf_size;
        buf_ptr = 0;
        write_pointer = slba; 

        while (size_to_io > 0) {
            // For every IO, calculate required size
            if (mdts_size < size_to_io) {
                single_io_size = mdts_size;
            } else {
                single_io_size = size_to_io;
            }

            char* new_buf = (char*) buffer + buf_ptr;
            lba_num = single_io_size / lba_size - ((single_io_size % lba_size) == 0 ? 1 : 0);
            
            // Perform IO
            if (read) {
                ret = nvme_read(fd, nsid, write_pointer, lba_num, 0, 0, 0, 0, 0, single_io_size, new_buf, 0, NULL);
                // printf("[DEBUG] READ WITH MDTS: %d\n", ret);
                // printf("nsid: %d, write_pointer: %lu, num_blocks: %lu\n", nsid, write_pointer, single_io_size / lba_size);
            } else {
                // TODO: Switch nvme_write() to nvme_zns_append() method.
                ret = nvme_write(fd, nsid, write_pointer, lba_num, 0, 0, 0, 0, 0, 0, single_io_size, new_buf, 0, NULL);
                //ret = nvme_zns_append(fd, nsid, write_pointer, single_io_size / lba_size - 1,
                //0, 0, 0, 0, single_io_size, (char*) buffer, 0, nullptr, lba_result);
                // printf("[DEBUG] WRITE WITH MDTS: %d\n", ret);
                // printf("nsid: %d, write_pointer: %lu, num_blocks: %lu\n", nsid, write_pointer, single_io_size / lba_size);
            }
            
            if (ret != 0) {
                printf("[ERROR] FAILED TO PERFORM IO WITH MDTS: %d\n", ret);
                return ret;
            }

            buf_ptr += single_io_size;
            size_to_io -= single_io_size;
            write_pointer += lba_num + 1;
        }
        return 0;
    }

    uint32_t free_zone_number(int offset) {
        return zns_metadata->log_zone_num_config - (zns_metadata->log_zone_end - zns_metadata->log_zone_start + offset) / zns_metadata->n_blocks_per_zone;
    }

    // find the next empty zone address
    int next_empty_zone() {
        for (uint64_t i = zns_metadata->log_zone_num_config; i < zns_device->tparams.zns_num_zones; i++) {
            if (zns_metadata->zone_states[i] == EMPTY_ZONE) {
                return i * zns_metadata->n_blocks_per_zone;
            }
        }
        return -1;
    }

    int zone_merge(std::unordered_map<int64_t, std::unordered_map<int64_t, int64_t>*> *zone_sets_ptr) {
        auto zone_set = *zone_sets_ptr;

        int64_t ret = -ENOSYS;
        int64_t num_blocks = zns_metadata->n_blocks_per_zone, lsb = zns_device->lba_size_bytes;
        char buffer[num_blocks * lsb] = {0};   

        auto iteration = zone_set.begin();
        for (iteration; iteration != zone_set.end(); iteration++) {
            UNUSED(iteration);
            int64_t zone_number = next_empty_zone(), prev_zone = -1;
            bool used_log = false;
            if (zone_number == -1) {
                zone_number = (zns_metadata->log_zone_num_config - 1) * zns_metadata->n_blocks_per_zone; // use the last zone of log zones
                used_log = true;
            }

            if (data_zone_mapping.find(iteration->first) != data_zone_mapping.end()) {
                ret = io_with_mdts(zns_metadata->fd, zns_metadata->nsid, data_zone_mapping[iteration->first], buffer, num_blocks * lsb, true);
                if (ret) {
                    printf("ERROR: failed during merging");
                    return ret;
                }
                
                zns_metadata->zone_states[data_zone_mapping[iteration->first] / num_blocks] = EMPTY_ZONE;
                prev_zone = data_zone_mapping[iteration->first];
            }
            
            auto map = *(iteration->second);
            auto j = map.begin();
            for (j; j != map.end(); j++) {
                UNUSED(j);
                ret = nvme_read(zns_metadata->fd, zns_metadata->nsid, j->second, 0, 0, 0, 0, 0, 0, lsb, buffer + lsb * j->first, 0, NULL);
                if (ret) {
                    printf("ERROR: failed to read log block at 0x%lx, ret: %ld\n", j->second, ret);
                    return ret;
                }
            }

            if (used_log) {
                nvme_zns_mgmt_send(zns_metadata->fd, zns_metadata->nsid, prev_zone, false, NVME_ZNS_ZSA_RESET, 0, NULL);
                ret = io_with_mdts(zns_metadata->fd, zns_metadata->nsid, prev_zone, buffer, num_blocks * lsb, false);
                if (ret) {
                    printf("ERROR: failed to write zone at 0x%lx, ret: %ld, used log zone\n", zone_number, ret);
                    return ret;
                }
                zns_metadata->zone_states[prev_zone / num_blocks] = FULL_ZONE;
                nvme_zns_mgmt_send(zns_metadata->fd, zns_metadata->nsid, zone_number, false, NVME_ZNS_ZSA_RESET, 0, NULL);
            } else {
                ret = io_with_mdts(zns_metadata->fd, zns_metadata->nsid, zone_number, buffer, num_blocks * lsb, false);
                if (ret) {
                    printf("ERROR: failed to write zone at 0x%lx, ret: %ld\n", zone_number, ret);
                    return ret;
                }
                data_zone_mapping[iteration->first] = zone_number;
                zns_metadata->zone_states[zone_number / num_blocks] = FULL_ZONE;

                if (prev_zone != -1)
                    nvme_zns_mgmt_send(zns_metadata->fd, zns_metadata->nsid, prev_zone, false, NVME_ZNS_ZSA_RESET, 0, NULL);
            }
            delete iteration->second;
        }
        return 0;
    }

    // trigger_gc() only takes args argument, can't take zns_device_metadata as a parameter
    // Other arguments beside args break pthread, since pthread's values and parameters have to be constant throughout the program
    void *trigger_gc(void *args) {
        struct zns_device_metadata *metadata = (struct zns_device_metadata *)args;
        while (true) {
            pthread_mutex_lock(&metadata->gc_mutex);
            while (!metadata->gc_thread_stop && !metadata->do_gc) {
                // signal
                pthread_cond_wait(&metadata->gc_wakeup, &metadata->gc_mutex);
            }

            if (metadata->gc_thread_stop) {
                pthread_mutex_unlock(&metadata->gc_mutex);
                break;
            }

            std::unordered_map<int64_t, std::unordered_map<int64_t, int64_t>*> zone_sets;
            std::unordered_map<int64_t, int64_t>::iterator iteration;
            for (iteration = log_zone_mapping.begin(); iteration != log_zone_mapping.end(); iteration++) {
                int64_t zone_number = ((iteration->first) / (zns_metadata->n_blocks_per_zone * zns_device->lba_size_bytes)) + zns_metadata->log_zone_num_config;
                if (!(zone_sets.find(zone_number) != zone_sets.end())) {
                    zone_sets[zone_number] = new std::unordered_map<int64_t, int64_t>;
                }
                auto map = zone_sets[zone_number];
                map->insert(std::pair<int64_t, int64_t>(((iteration->first) % (zns_metadata->n_blocks_per_zone * zns_device->lba_size_bytes) / zns_device->lba_size_bytes), iteration->second));
            }
            int ret = zone_merge(&zone_sets);
            
            if (ret) {
                printf("Error: GC failed, ret:%d\n", ret);
            }

            for (int i = 0; i < metadata->log_zone_num_config; i++) {
                nvme_zns_mgmt_send(metadata->fd, metadata->nsid, i * metadata->n_blocks_per_zone, false, NVME_ZNS_ZSA_RESET, 0, NULL);
            }

            metadata->log_zone_end = metadata->log_zone_start;
            log_zone_mapping.clear();
            metadata->do_gc = false;
            pthread_cond_signal(&metadata->gc_sleep);
            pthread_mutex_unlock(&metadata->gc_mutex);
        }
        return (void *)0;
    }
        
    int deinit_ss_zns_device(struct user_zns_device *my_dev) {
        int ret = -ENOSYS;

        //struct zns_device_metadata *metadata = (struct zns_device_metadata *)my_dev->_private;
        auto *metadata = (struct zns_device_metadata *)my_dev->_private;
        metadata->gc_thread_stop = true;
        pthread_cond_signal(&metadata->gc_wakeup);

        // wait for gc stop
        pthread_join(metadata->gc_thread_id, NULL);

        pthread_mutex_destroy(&metadata->gc_mutex);
        pthread_cond_destroy(&metadata->gc_wakeup);
        ret = close(metadata->fd);
        
        if (ret != 0) {
            printf("[ERROR] FAILED TO CLOSE FILE DESC: %d\n", ret);
            return ret;
        }

        free(metadata->zone_states);
        free(my_dev->_private);
        free(my_dev);
        
        return ret;
    }

    int init_ss_zns_device(struct zdev_init_params *params, struct user_zns_device **my_dev) {
        int ret = -ENOSYS;

        int fd = nvme_open(params->name);
        if (fd < 0) {
            printf("[ERROR] FAILED TO OPEN FILE DESC: %d\n", fd);
            return ret;
        }

        auto *metadata = static_cast<struct zns_device_metadata *>(calloc(sizeof(struct zns_device_metadata), 1));
        (*my_dev) = static_cast<struct user_zns_device *>(calloc(sizeof(struct user_zns_device), 1));
        
        metadata->fd = fd;
        metadata->gc_watermark = params->gc_wmark;
        metadata->log_zone_num_config = params->log_zones;
        (*my_dev)->_private = metadata;
        
        /**
        * Device Identification Phase
        * Mainly for collecting related attributes of the zns device
        */

        // Get namespace id
        ret = nvme_get_nsid(fd, &metadata->nsid);
        if (ret != 0) {
            printf("[ERROR] FAILED TO GET NAMESPACE ID: %d\n", ret);
            return ret;
        }

        // Get namespace metadata
        struct nvme_id_ns ns{};
        ret = nvme_identify_ns(fd, metadata->nsid, &ns);
        if (ret != 0) {
            printf("[ERROR] FAILED TO GET NAMESPACE METADATA: %d\n", ret);
            return ret;
        }

        // Get zone report (for a single one)
        struct nvme_zone_report single_zone_report{};
        ret = nvme_zns_mgmt_recv(fd, metadata->nsid, 0, NVME_ZNS_ZRA_REPORT_ZONES, NVME_ZNS_ZRAS_REPORT_ALL, false, sizeof(single_zone_report), (void *)&single_zone_report);
        if (ret != 0) {
            printf("[ERROR] FAILED TO REPORT ZONE INFO: %d\n", ret);
            return ret;
        }

        (*my_dev)->tparams.zns_num_zones = single_zone_report.nr_zones;
        metadata->zone_states = (uint8_t *)calloc(single_zone_report.nr_zones, sizeof(uint8_t));

        if (params->force_reset)
        {
            ret = nvme_zns_mgmt_send(fd, metadata->nsid, 0, true, NVME_ZNS_ZSA_RESET, 0, NULL);
            if (ret)
            {
                printf("ERROR: failed to reset all zones %d \n", ret);
                return ret;
            }

            // metadata->data_zone_start = metadata->data_zone_end = params->log_zones * n_blocks_per_zone;
            metadata->log_zone_start = metadata->log_zone_end = 0;
        }

        metadata->mdts = get_mdts_size(metadata->fd);
        
        // Get zone report (for all zones)
        // After getting the one single_zone_report, we now know the number of zones
        // Then we proceed to getting reports of all zones
        uint64_t all_zone_reports_size = sizeof(single_zone_report) + (single_zone_report.nr_zones * sizeof(struct nvme_zns_desc));
        char *all_zone_reports= (char *)calloc(1, all_zone_reports_size);
        ret = nvme_zns_mgmt_recv(fd, metadata->nsid, 0, NVME_ZNS_ZRA_REPORT_ZONES, NVME_ZNS_ZRAS_REPORT_ALL, 1, all_zone_reports_size, (void *)all_zone_reports);
        // For the time being the reported zone numbers should be 32,
        // so ((struct nvme_zone_report *)all_zone_reports)->entries[31] should be the last zone
        if (ret != 0) {
            printf("[ERROR] FAILED TO REPORT ALL ZONE INFO: %d\n", ret);
            free(all_zone_reports);
            return ret;
        }

        /**
        * Attributes & Data Population Phase
        * Mainly for calculating and filling in corresponding attributes data
        * to the metadata and related struct
        */
        (*my_dev)->lba_size_bytes = 1 << ns.lbaf[(ns.flbas & 0xf)].ds;
        (*my_dev)->tparams.zns_lba_size = (*my_dev)->lba_size_bytes;
        // (*my_dev)->tparams.zns_num_zones = single_zone_report.nr_zones;
        uint64_t n_blocks_per_zone = ((struct nvme_zone_report *)all_zone_reports)->entries[0].zcap;
        //metadata->n_blocks_per_zone = n_blocks_per_zone;
        (*my_dev)->tparams.zns_zone_capacity = n_blocks_per_zone * (*my_dev)->lba_size_bytes;
        (*my_dev)->capacity_bytes = (single_zone_report.nr_zones - params->log_zones) * ((*my_dev)->tparams.zns_zone_capacity);

        // For Milestone 2, GC watermark and two "pointers" chasing each other
        // metadata->gc_watermark = params->gc_wmark;
        metadata->log_zone_start = 0;
        metadata->log_zone_end = 0;
        metadata->data_zone_start = params->log_zones * n_blocks_per_zone;
        metadata->data_zone_end = params->log_zones * n_blocks_per_zone;
        metadata->n_blocks_per_zone = n_blocks_per_zone;
        metadata->n_log_zone = params->log_zones;

        for (uint64_t i = params->log_zones; i < single_zone_report.nr_zones; i++) {
            metadata->zone_states[i] = (((struct nvme_zone_report *)all_zone_reports)->entries[i].zs >> 4);
        }

        free(all_zone_reports);

        ret = pthread_create(&metadata->gc_thread_id, NULL, &trigger_gc, metadata);
        if (ret) {
            printf("ERROR: failed to create gc thread %d \n", ret);
            return ret;
        }

        zns_device = *my_dev;
        zns_metadata = metadata;

        // Reset the device if required
        if (params->force_reset) {
            ret = nvme_zns_mgmt_send(fd, metadata->nsid, 0, true, NVME_ZNS_ZSA_RESET, 0, NULL);
            if (ret != 0) {
                printf("[ERROR] FAILED TO RESET ALL ZONES: %d\n", ret);
                return ret;
            }
        }

        return 0;
    }

    int zns_udevice_read(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size) {
        if (size % my_dev->lba_size_bytes) {
            printf("INVALID: read size not aligned to block size\n");
            return -1;
        }

        int32_t ret, lba_s = my_dev->lba_size_bytes;
        uint32_t blocks = size / lba_s, num_read = 0;
        auto *metadata = (struct zns_device_metadata *)my_dev->_private;
        for (uint64_t i = address; i < address + blocks * lba_s; i += lba_s) {
            uint64_t entry;
            bool read_data = true;
            if ((log_zone_mapping.find(i) != log_zone_mapping.end())) {
                entry = log_zone_mapping[i];
                read_data = (entry & (1L << 63));
            }

            if (read_data) {
                uint64_t zone_number = ((i) / (zns_metadata->n_blocks_per_zone * zns_device->lba_size_bytes)) + zns_metadata->log_zone_num_config;
                if (!(data_zone_mapping.find(zone_number) != data_zone_mapping.end())) {
                    memset(buffer, 0, size);
                    return 0;
                }

                entry = data_zone_mapping[zone_number] + ((i) % (zns_metadata->n_blocks_per_zone * zns_device->lba_size_bytes) / zns_device->lba_size_bytes);
            }

            if (size < metadata->mdts) {
                ret = nvme_read(metadata->fd, metadata->nsid, (entry & ~(1L << 63)), 0, 0, 0, 0, 0, 0, lba_s, (char *)buffer + num_read, 0, NULL);
                if (ret) {
                    printf("ERROR: failed to read at 0x%lx, ret: %d\n", entry & ~(1L << 63), ret);
                    return ret;
                }
                num_read += lba_s;
            } else {
                ret = io_with_mdts(metadata->fd, metadata->nsid, (entry & ~(1L << 63)), (char *)buffer + num_read, size, true);
                if (ret) {
                    printf("ERROR: failed to read at 0x%lx, ret: %d\n", entry & ~(1L << 63), ret);
                    return ret;
                }
                num_read += lba_s;
            }
        }

        return 0;
    }

    int zns_udevice_write(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size)  {
        if (size % my_dev->lba_size_bytes) {
            printf("INVALID: write size not aligned to block size\n");
            return -1;
        }

        auto *metadata = (struct zns_device_metadata *)my_dev->_private;
        uint32_t blocks = size / my_dev->lba_size_bytes;
        __u64 lba_result = 0;
        // Starting lock here
        // zns_metadata has to be consistent throughout the program, hence declaring it globally instead of passing metadata by reference.
        pthread_mutex_lock(&zns_metadata->gc_mutex);
        while (free_zone_number(blocks) <= metadata->gc_watermark) {
            zns_metadata->do_gc = true;
            pthread_cond_signal(&zns_metadata->gc_wakeup);
            pthread_cond_wait(&zns_metadata->gc_sleep, &zns_metadata->gc_mutex);
        }

        int32_t ret, prev_log_zone_end = metadata->log_zone_end, zone_no = metadata->log_zone_end / metadata->n_blocks_per_zone;
        
        if (size < metadata->mdts) {
            ret = nvme_zns_append(metadata->fd, metadata->nsid, zone_no * metadata->n_blocks_per_zone, blocks - 1, 0, 0, 0, 0, size, buffer, 0, NULL, &lba_result);
            if (ret != 0) {
                printf("[ERROR] FAILED TO WRITE TO DEVICE: %d\n", ret);
                return ret;
            }
            
            metadata->log_zone_end = lba_result + 1;
        } else {
            ret = io_with_mdts(metadata->fd, metadata->nsid, zone_no * metadata->n_blocks_per_zone, buffer, size, false);
            if (ret != 0) {
                printf("[ERROR] FAILED TO WRITE TO DEVICE: %d\n", ret);
                return ret;
            }
        }

        for (uint32_t i = 0; i < blocks; i++) {
            log_zone_mapping[address + i * my_dev->lba_size_bytes] = prev_log_zone_end + i;
        }

        pthread_mutex_unlock(&zns_metadata->gc_mutex);
        return 0;
    }
}