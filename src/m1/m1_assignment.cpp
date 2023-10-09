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

#include <assert.h>
#include <errno.h>
#include <stdio.h>

// User imported
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <sys/mman.h>
#include <cinttypes>
#include <string>

#include "m1_assignment.h"
#include "../common/unused.h"

extern "C"
{

int ss_nvme_device_io_with_mdts(int fd, uint32_t nsid, uint64_t slba, uint16_t numbers, void *buffer, uint64_t buf_size,
                                uint64_t lba_size, uint64_t mdts_size, bool read){
    int ret = -ENOSYS;

    uint64_t size_to_io, write_pointer, buf_ptr, single_io_size;
    size_to_io = buf_size;
    buf_ptr = 0;
    write_pointer = slba;

    while (size_to_io) {
        // For every IO, calculate required size
        if (mdts_size < size_to_io) {
            single_io_size = mdts_size;
        } else {
            single_io_size = size_to_io;
        }

        char *new_buf = (char*) buffer + buf_ptr;

        // Perform IO
        if (read) {
            ret = ss_nvme_device_read(fd, nsid, write_pointer, single_io_size / lba_size, new_buf, single_io_size);
        } else {
            ret = ss_nvme_device_write(fd, nsid, write_pointer, single_io_size / lba_size, new_buf, single_io_size);
        }

        if (ret != 0) {
            printf("[ERROR] FAILED TO PERFORM IO");
            return ret;
        }

        // Update all related symbols/pointers
        write_pointer = write_pointer + single_io_size / lba_size;
        size_to_io = size_to_io - single_io_size;
        buf_ptr = buf_ptr + single_io_size;
    }

    return ret;
}

int ss_nvme_device_read(int fd, uint32_t nsid, uint64_t slba, uint16_t numbers, void *buffer, uint64_t buf_size) {
    int ret; //= -ENOSYS;
    // this is to supress gcc warnings, remove it when you complete this function 
    /* UNUSED(fd);
    UNUSED(nsid);
    UNUSED(slba);
    UNUSED(numbers);
    UNUSED(buffer);
    UNUSED(buf_size);
    */
    ret = nvme_read(fd, nsid, slba, numbers-1, 0, 0, 0, 0, 0, (__u32) buf_size, buffer, 0, nullptr);
    return ret;
}

int ss_nvme_device_write(int fd, uint32_t nsid, uint64_t slba, uint16_t numbers, void *buffer, uint64_t buf_size) {
    int ret; //= -ENOSYS;
    // this is to supress gcc warnings, remove it when you complete this function
    /*
    UNUSED(fd);
    UNUSED(nsid);
    UNUSED(slba);
    UNUSED(numbers);
    UNUSED(buffer);
    UNUSED(buf_size);
    */

    ret = nvme_write(fd, nsid, slba, numbers - 1, 0, 0, 0, 0, 0, 0, (__u32) buf_size, buffer, 0, nullptr);
    return ret;
}

int ss_zns_device_zone_reset(int fd, uint32_t nsid, uint64_t slba) {
    int ret; //= -ENOSYS;
    // this is to supress gcc warnings, remove it when you complete this function 
    //UNUSED(fd);
    //UNUSED(nsid);
    //UNUSED(slba);
    ret = nvme_zns_mgmt_send(fd, nsid, slba, false, NVME_ZNS_ZSA_RESET, 0, nullptr);  
    return ret;
}

// this does not take slba because it will return that
int ss_zns_device_zone_append(int fd, uint32_t nsid, uint64_t zslba, int numbers, void *buffer, uint32_t buf_size, uint64_t *written_slba){
    //see section 4.5 how to write an append command
    int ret; // = -ENOSYS;
    // this is to supress gcc warnings, remove it when you complete this function 
    /*
    UNUSED(fd);
    UNUSED(nsid);
    UNUSED(zslba);
    UNUSED(numbers);
    UNUSED(buffer);
    UNUSED(buf_size);
    UNUSED(written_slba);
    */

    ret = nvme_zns_append(fd, nsid, zslba, (__u16) numbers - 1, 0, 0, 0, 0, buf_size, buffer, 0, nullptr, (__u64*) written_slba); 
    return ret;
}

void update_lba(uint64_t &write_lba, const uint32_t lba_size, const int count){
    write_lba += count;
}

// see 5.15.2.2 Identify Controller data structure (CNS 01h)
// see how to pass any number of variables in a C/C++ program https://stackoverflow.com/questions/1579719/variable-number-of-parameters-in-function-in-c
// feel free to pass any relevant function parameter to this function extract MDTS 
// you must return the MDTS as the return value of this function 
static void *mmap_registers(int fd)
{
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
    __u16	mqes;
    __u8	ams_cqr;
    __u8	to;
    __u16	bps_css_nssrs_dstrd;
    __u8	mpsmax_mpsmin;
    __u8	rsvd_cmbs_pmrs;
};

// see 5.15.2.2 Identify Controller data structure (CNS 01h)
// see how to pass any number of variables in a C/C++ program https://stackoverflow.com/questions/1579719/variable-number-of-parameters-in-function-in-c
// feel free to pass any relevant function parameter to this function extract MDTS
// you must return the MDTS as the return value of this function
uint64_t get_mdts_size(int fd, ...) {
    int ret = -ENOSYS;
    struct nvme_id_ctrl ctrl{};
    ret = nvme_identify_ctrl(fd, &ctrl);
    if (ret != 0) {
        printf("[ERROR] FAILED TO IDENTIFY DEVICE: %d\n", ret);
    }

    // NOTE(caesar): Sorry but fabrics does not work
    // ret = nvme_get_property(fd, _offset, mps_min);
    // NOTE(caesar): Gonna use the good old ways (mostly from the source code of nvme-cli)
    void *membase = mmap_registers(fd);
    // TODO(caesar): Originally the code from nvme-cli writes like this.
    //               However, GCC complains that arithmetics on void* pointers are not allowed.
    //               Since NVME_REG_CAP is 0x0 anyway, I am going to remove the addition.
    // const volatile __u32 *p = (__u32 *)(membase + NVME_REG_CAP);

    const volatile __u32 *p = (__u32 *)(membase);
    __u32 low, high;

    low = le32_to_cpu(*p);
    high = le32_to_cpu(*(p + 1));

    uint64_t cap = ((__u64) high << 32) | low;
    auto* cap_struct = (struct nvme_bar_cap *)&cap;
    int mps_min = 1 << (12 + (cap_struct->mpsmax_mpsmin & 0x0f));

    // Workaround for QEMU NVMe Controller Bug
    return pow(2, ctrl.mdts - 1) * mps_min;
    // Without the bug
//     return pow(2, ctrl.mdts) * mps_min;
}
}