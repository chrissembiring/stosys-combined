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

#include <cerrno>
#include <cstdint>
#include <libnvme.h>
#include <iostream>
#include "zns_device.h"
#include <pthread.h>
#include <unordered_map>
#include "../common/unused.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <iostream>
#include <map>
#include <vector>

pthread_t gc_pthread = 0;
bool gc_working = true;
bool GC_this = true;
bool gc_deinit = false;
pthread_mutex_t lockGC = PTHREAD_MUTEX_INITIALIZER;
pthread_rwlock_t rwlock = PTHREAD_RWLOCK_INITIALIZER;

#define map_contains(map, key) (map.find(key) != map.end())

extern "C"
{

    uint64_t find_next_empty_data_zone2(struct user_zns_device *my_dev)

    {
        uint64_t incorrect_zone = ((dev_props *)my_dev->_private)->num_zones + 10;
        for (int i = ((dev_props *)my_dev->_private)->number_of_log_zones + ((dev_props *)my_dev->_private)->gc_wmark; i < ((dev_props *)my_dev->_private)->num_zones; i++)
        {
            if (((dev_props *)my_dev->_private)->is_zone_empty_array[i] == true)
            {
                return i;
            }
        }
        return incorrect_zone;
    }

    void print_log(struct user_zns_device *my_dev)
    {
        std::cout << "Contents of the map:" << std::endl;
        for (const auto &pair : ((dev_props *)my_dev->_private)->log_mapping)
        {
            std::cout << "user_provided_lba: " << pair.first << ", zslba: " << pair.second << std::endl;
        }
    }

    void *gc(void *args)
    {

        struct user_zns_device *my_dev = (struct user_zns_device *)args;

        // Access members of my_dev

        while (gc_working)
        {
            // if(gc_working)
            //{
            // pthread_mutex_lock(&lockGC);
            while (!GC_this)
            {
                // RW Problem
                pthread_rwlock_unlock(&rwlock);
            }
            printf("\nGC LOOP START\n");

            uint64_t nlb = ((dev_props *)my_dev->_private)->nlb;
            uint64_t lba_size_bytes = my_dev->lba_size_bytes;
            int fd = ((dev_props *)my_dev->_private)->fd;
            uint32_t nsid = ((dev_props *)my_dev->_private)->nsid;
            int lba_size = ((dev_props *)my_dev->_private)->lba_size;

            // Switch merge for mappings only
            // zone_lba_to_pba = for each target zone(where each lba should be) map current lba to pba
            std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t> *> zone_lba_to_pba;
            for (const auto &pair : ((dev_props *)my_dev->_private)->log_mapping)
            {
                // Division is for Zone Number
                
                uint64_t target_zone = (pair.first / nlb); // + ((dev_props *)my_dev->_private)->number_of_log_zones + ((dev_props *)my_dev->_private)->gc_wmark;
                uint64_t address_offset_in_zone = pair.first % nlb;

                printf("lba: %d \t pba: %d \n target_zone %d \n address_offset_in_zone %d \n", pair.first, pair.second, target_zone, address_offset_in_zone);
                if (!map_contains(zone_lba_to_pba, target_zone)) // create zone mapping if doesn't
                {
                    zone_lba_to_pba[target_zone] = new std::unordered_map<uint64_t, uint64_t>;
                }

                auto map = zone_lba_to_pba[target_zone];

                map->insert(std::pair<uint64_t, uint64_t>(address_offset_in_zone, pair.second));
                // pair.second &= ENTRY_INVALID; flag
            };

            // print zone_lba_to_pba for target zone 0

            //  if (map_contains(zone_lba_to_pba, 0)) // create zone mapping if doesn't
            //  {
            //       printf("\n//print zone_lba_to_pba for target zone 0 \n");
            //     for (const auto &pair : *(zone_lba_to_pba[0]))
            //{
            //        std::cout << "user_provided_lba: " << pair.first << ", zslba: " << pair.second << std::endl;
            //   }
            // }

            // create reusable buffer
            char temp_buffer[nlb * lba_size_bytes] = "";
            bool reuse_same_zone = false;
            for (const auto &pair : zone_lba_to_pba) // for each target zone, we need at least one entry
            {
                int64_t data_zone_index = find_next_empty_data_zone2(my_dev); // index of data zone where we will store our data
                uint64_t target_zone = pair.first;
                uint64_t incorrect_zone = ((dev_props *)my_dev->_private)->num_zones + 10;
                uint64_t old_zone = incorrect_zone;

                printf("target_zone -> %d \t next_empty_data_zone -> %d", target_zone, data_zone_index);

                auto it = ((dev_props *)my_dev->_private)->data_zone_mapping.find(target_zone);
                if (it != ((dev_props *)my_dev->_private)->data_zone_mapping.end())
                {
                    old_zone = it->second;
                }
                else
                {
                    ; // std::cout << "Key " << target_zone << " not found" << std::endl;
                }

                if (data_zone_index == -1)
                {
                    // where to store if none are empty

                    // use old zone

                    data_zone_index = old_zone;

                    reuse_same_zone = true;
                }

                // copy old data to buffer
                if (old_zone != incorrect_zone)
                {

                    for (int i = 0; i < nlb; i++)
                    {
                        uint64_t offset = lba_size_bytes * i;
                        nvme_read(fd, nsid, old_zone * nlb, 0, 0, 0, 0, 0, 0, lba_size, temp_buffer + offset, 0, nullptr);
                    }
                }

                std::unordered_map<uint64_t, uint64_t> *inner_map = pair.second;

                // read each entry from zone_lba_to_pba and place at correct place of buffer
                for (const auto &inner_pair : *inner_map)
                {
                    uint64_t user_provided_lba = inner_pair.first;
                    uint64_t slba = inner_pair.second;
                    //printf("lba %d", user_provided_lba);
                    uint64_t offset = lba_size_bytes * user_provided_lba;
                    // user_provided_lba
                    uint64_t nlb = 0; // TODO check if correct
                    int ret = nvme_read(fd, nsid, slba, nlb, 0, 0, 0, 0, 0, lba_size, temp_buffer + offset, 0, nullptr);
                }
                printf("\n");
                uint64_t zslba = data_zone_index * nlb;
                uint64_t old_zone_zslba = old_zone * nlb;
                uint64_t nr_of_blocks = 0; // we write one block at the time

                // writing data
                // if we reuse same zone
                // first reset, write
                if (reuse_same_zone)
                {

                    printf("reuse same zone, data_zone_index %d,  old_zone %d \n", data_zone_index, old_zone);
                    // use the the last zone of log for backup
                    nvme_zns_mgmt_send(fd, nsid, old_zone_zslba, false, NVME_ZNS_ZSA_RESET, 0, NULL);
                    // append nvme in chunks
                    for (int i = 0; i < nlb; i++)
                    {
                        nvme_zns_append(fd, nsid, zslba, nr_of_blocks, 0, 0, 0, 0, lba_size, temp_buffer + (i * lba_size), 0, NULL, nullptr);
                    }
                }
                else // if new zone
                {

                    // write data, reset old one
                    printf("use new zone, data_zone_index %d,  old_zone %d \n", data_zone_index, old_zone);
                    // append nvme in chunks
                    for (int i = 0; i < nlb; i++)
                    {
                        nvme_zns_append(fd, nsid, zslba, nr_of_blocks, 0, 0, 0, 0, lba_size, temp_buffer + (i * lba_size), 0, NULL, nullptr);
                    }
                    ((dev_props *)my_dev->_private)->is_zone_empty_array[data_zone_index] = false;

                    // update data zone mapping (target_zone, data_zone_index)
                    auto it = ((dev_props *)my_dev->_private)->data_zone_mapping.find(target_zone);
                    if (it != ((dev_props *)my_dev->_private)->data_zone_mapping.end())
                    {
                        it->second = data_zone_index;
                    }
                    else
                    {
                        ((dev_props *)my_dev->_private)->data_zone_mapping.insert({target_zone, data_zone_index});
                    }
                    // reset old one
                    if (old_zone != incorrect_zone)
                    {
                        printf("reset old zone, data_zone_index %d,  old_zone %d \n", data_zone_index, old_zone);
                        nvme_zns_mgmt_send(fd, nsid, old_zone_zslba, false, NVME_ZNS_ZSA_RESET, 0, NULL);
                        ((dev_props *)my_dev->_private)->is_zone_empty_array[old_zone] = true;
                    }
                }
            }

            // printf("priting BUFFER->\t");
            // printf("%c \t", temp_buffer[0]);
            // printf("%c \t", temp_buffer[4096]);
            // printf("%c END", temp_buffer[8192]);
            // printf("\n");

            std::cout << "Contents of the data zone map:" << std::endl;
            for (const auto &pair : ((dev_props *)my_dev->_private)->data_zone_mapping)
            {
                std::cout << "target_zone, : " << pair.first << ", data_zone_index: " << pair.second << std::endl;
            }

            for (auto &pair : zone_lba_to_pba)
            {
                delete pair.second;
            }
            zone_lba_to_pba.clear();
            usleep(500000); // Sleep for 100,000 microseconds (0.1 seconds)
            //}
            // pthread_rwlock_unlock(&rwlock);
        }

        return 0;
    }

    int deinit_ss_zns_device(struct user_zns_device *my_dev)
    {
        gc_working = false;
        bool gc_deinit = true;

        pthread_join(gc_pthread, NULL);
        ((dev_props *)my_dev->_private)->log_mapping.clear();
        ((dev_props *)my_dev->_private)->data_zone_mapping.clear();

        delete[] ((dev_props *)my_dev->_private)->is_zone_empty_array;
        free(my_dev);

        return 0;
    }

    int init_ss_zns_device(struct zdev_init_params *params, struct user_zns_device **my_dev)
    {
        int fd;
        uint32_t nsid;
        // finding device props
        fd = nvme_open(params->name);
        nvme_get_nsid(fd, &nsid);

        struct nvme_id_ns ns
        {
        };
        nvme_identify_ns(fd, nsid, &ns);

        struct nvme_zns_id_ns zns_id_ns;
        nvme_zns_identify_ns(fd, nsid, &zns_id_ns);

        struct nvme_zns_id_ctrl zns_id_ctrl;
        nvme_zns_identify_ctrl(fd, &zns_id_ctrl);

        struct nvme_zone_report zns_report
        {
        };
        nvme_zns_mgmt_recv(fd, nsid, 0, NVME_ZNS_ZRA_REPORT_ZONES, NVME_ZNS_ZRAS_REPORT_ALL, 0, sizeof(zns_report), (void *)&zns_report);

        // 256 = 1MB/4KB (Zone / Block) = n_pba for this case
        // int n_zones = int(zns_report.nr_zones);
        // 0 based counting, check id-ns
        // int number_of_zone_inside_ns = ns.nlbaf + 1;
        // std::cout << "number_of_zone_inside_ns" << number_of_zone_inside_ns << std::endl;

        // std::cout << "ns.nsze: " << ns.nsze << "\n";
        // std::cout << "z_size: " << z_size << std::endl;

        int ns_no_of_flbas = ns.flbas & 0xf;
        int ds_value = ns.lbaf[ns_no_of_flbas].ds;
        int lba_size = 1 << ds_value;
        // std::cout << "lba_size: " << lba_size << std::endl;

        uint64_t zns_report_full_size = sizeof(zns_report) + (zns_report.nr_zones * sizeof(struct nvme_zns_desc));
        char *report_zones = (char *)calloc(1, zns_report_full_size);
        nvme_zns_mgmt_recv(fd, nsid, 0, NVME_ZNS_ZRA_REPORT_ZONES, NVME_ZNS_ZRAS_REPORT_ALL, 1, zns_report_full_size, (void *)report_zones);
        // choose any zone, all nlb's
        uint64_t nlb = ((struct nvme_zone_report *)report_zones)->entries[0].zcap;
        // lets free it immediately, cuz we only need nlb :)
        free(report_zones);
        // z_size_in_KB / lba_size;
        // std::cout << "nlb: " << nlb << std::endl;

        int log_zone_capacity = (params->log_zones * lba_size * nlb);
        int num_zones = le64_to_cpu(zns_report.nr_zones);
        int capacity = (num_zones * lba_size * nlb) - log_zone_capacity;

        if (params->force_reset)
            for (int i = 0; i < num_zones; i += nlb)
            {
                reset_whole_zone(fd, nsid, i);
            }

        struct dev_props dev_props;
        dev_props.fd = fd;
        dev_props.nlb = nlb;
        dev_props.nsid = nsid;
        dev_props.s_nsid = &ns;
        dev_props.zns_id_ctrl = &zns_id_ctrl;
        dev_props.zns_id_ns = &zns_id_ns;
        dev_props.lba_size = lba_size;
        dev_props.current_write_pointer = 0;
        dev_props.current_log_zone = 0;
        dev_props.current_data_zone = params->log_zones + 1;
        dev_props.num_zones = num_zones;
        dev_props.number_of_log_zones = params->log_zones;
        dev_props.gc_wmark = params->gc_wmark;

        dev_props.is_zone_empty_array = new bool[num_zones];
        for (int i = 0; i < num_zones; i++)
        {
            dev_props.is_zone_empty_array[i] = true;
        }

        // std::cout << "num_zones: " << num_zones << std::endl;

        struct zns_device_testing_params tparams;
        tparams.zns_lba_size = lba_size;
        tparams.zns_num_zones = num_zones;
        tparams.zns_zone_capacity = lba_size * nlb;

        *my_dev = (user_zns_device *)calloc(1, sizeof(user_zns_device));

        (*my_dev)->capacity_bytes = capacity;
        (*my_dev)->lba_size_bytes = lba_size;
        (*my_dev)->tparams = tparams;
        (*my_dev)->_private = &dev_props;

        struct ThreadData data;
        data.my_dev = *my_dev;

        pthread_create(&gc_pthread, NULL, gc, (void *)*my_dev);

        return 0;
    }

    int zns_udevice_read(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size)
    {
        int lba_size = ((dev_props *)my_dev->_private)->lba_size;
        uint32_t max_size = (int)lba_size;
        // doing this so that we avoid conflict
        uint64_t max_lba_entries = my_dev->capacity_bytes / my_dev->lba_size_bytes;
        // splitting data buffer into block chunks and performing I/O on them
        // pthread_rwlock_rdlock(&rwlock);
        for (uint32_t offset = 0; offset < size; offset += max_size)
        {
            void *chunk_buffer = (char *)buffer + offset;
            // std::cout << "READ address + offset: " << address + offset << std::endl;
            // std::cout << "READ address" << address << std::endl;
            // int ret = read_io(my_dev, (address + 0) * max_lba_entries + offset, chunk_buffer, max_size);
            int ret = read_io(my_dev, address + offset, chunk_buffer, max_size);
            // print_log(my_dev);
            if (ret != 0)
            {
                return ret;
            }
        }
        // pthread_rwlock_unlock(&rwlock);
        return 0;
    }

    int read_io(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size)
    {
        int fd = ((dev_props *)my_dev->_private)->fd;
        uint64_t slba = ((dev_props *)my_dev->_private)->log_mapping[address];
        uint32_t nsid = ((dev_props *)my_dev->_private)->nsid;
        // blocks are 0 based, so block count - 1
        int ret = nvme_read(fd, nsid, slba, (size / my_dev->lba_size_bytes) - 1, 0, 0, 0, 0, 0, size, buffer, 0, nullptr);
        return ret;
    }

    int zns_udevice_write(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size)
    {
        int lba_size = ((dev_props *)my_dev->_private)->lba_size;
        uint32_t max_size = lba_size;
        // doing this so that we avoid conflict
        uint64_t max_lba_entries = my_dev->capacity_bytes / my_dev->lba_size_bytes;
        // splitting data buffer into block chunks and performing I/O on them
        // GC Trigger Here
        int n_of_lz_to_trigger_gc_in_lba = ((((dev_props *)my_dev->_private)->number_of_log_zones - ((dev_props *)my_dev->_private)->gc_wmark)) * ((dev_props *)my_dev->_private)->nlb;
        if (((dev_props *)my_dev->_private)->current_log_zone == n_of_lz_to_trigger_gc_in_lba)
        {
            ; // gc_working = true;
        }
        // pthread_rwlock_wrlock(&rwlock);
        for (uint32_t offset = 0; offset < size; offset += max_size)
        {
            void *chunk_buffer = (char *)buffer + offset;
            // std::cout << "WRITE address + offset: " << address + offset << std::endl;
            // std::cout << "WRITE address" << address << std::endl;
            // int ret = write_io(my_dev, (address + 0) * max_lba_entries + offset, chunk_buffer, max_size);
            int ret = write_io(my_dev, address + offset, chunk_buffer, max_size);
            if (ret != 0)
            {
                return ret;
            }
        }
        // pthread_rwlock_wrlock(&rwlock);
        return 0;
    }

    int write_io(struct user_zns_device *my_dev, uint64_t address, void *buffer, uint32_t size)
    {
        // char* strBuffer = static_cast<char*>(buffer);
        // std::cout << "Printing at the start of the write func, the buffer to match is: " << strBuffer << std::endl;
        // std::cout << "Printing at the start of the func, Writing is done !!, the user lba is: " << address << std::endl;
        int fd = ((dev_props *)my_dev->_private)->fd;
        uint32_t nsid = ((dev_props *)my_dev->_private)->nsid;
        uint64_t zslba = ((dev_props *)my_dev->_private)->current_write_pointer;
        int nlb = ((dev_props *)my_dev->_private)->nlb;
        if (zslba % nlb == 0 && zslba != 0)
        {
            ((dev_props *)my_dev->_private)->current_log_zone += nlb;
        }
        int current_log_zone = ((dev_props *)my_dev->_private)->current_log_zone;
        // std::cout << "PRINT LOG FOR THE WRITE FUNC !!: " << std::endl;
        // print_log(my_dev);
        short filled_blocks = (size / my_dev->lba_size_bytes) - 1;
        ((dev_props *)my_dev->_private)->current_write_pointer += filled_blocks + 1;
        // std::cout << "current log zone: " << current_log_zone << std::endl;
        uint64_t ret = nvme_zns_append(fd, nsid, current_log_zone, filled_blocks, 0, 0, 0, 0, size, buffer, 0, NULL, nullptr);
        if (ret == 0)
        {
            auto it = ((dev_props *)my_dev->_private)->log_mapping.find(address);
            if (it != ((dev_props *)my_dev->_private)->log_mapping.end())
            {
                it->second = zslba;
            }
            else
            {
                ((dev_props *)my_dev->_private)->log_mapping.insert({address, zslba});
            }
        }

        // std::cout << "Printing at the end of the write func, zslba, and it hasn't been changed by the filled blocks variable :" << zslba << "\n";
        return ret;
    }

    int reset_whole_zone(int fd, uint32_t nsid, uint64_t slba)
    {
        int ret = nvme_zns_mgmt_send(fd, nsid, slba, true, NVME_ZNS_ZSA_RESET, 0, NULL);
        return ret;
    }
}