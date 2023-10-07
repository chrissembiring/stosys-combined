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
#include <stdarg.h>
#include <sys/mman.h>
 
#include "m1_assignment.h"
#include "../common/unused.h"

extern "C"
{

int ss_nvme_device_io_with_mdts(int fd, uint32_t nsid, uint64_t slba, uint16_t numbers, void *buffer, uint64_t buf_size,
                                uint64_t lba_size, uint64_t mdts_size, bool read){
    int ret; // = -ENOSYS;
    ret = get_mdts_size(fd);

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
uint64_t get_mdts_size(int fd, ...){    
    //UNUSED(count);
    // Variadic functions, use later
    /*
    int val = 0;
    va_list ap;
    int i;

    va_start(ap, count);
    for (i = 0; i < count; i++) {
        val += va_arg(ap, int);
    }
    va_end(ap);
    return val;
    
    void* membase = mmap(NULL, getpagesize(), PROT_READ, MAP_SHARED, fd, 0);
    uint64_t cap_value= nvme_mmio_read64(membase + NVME_REG_CAP);
    // int mpsmax = 1 << (12 + (nvme_show_registers_cap(cap_value) >> 48)) & 0xF0;
    int mpsmin = 1 << (12 + (nvme_show_registers_cap(cap_value)) & 0x0F);

    struct nvme_id_ctrl* ctrl;
    nvme_id_ctrl(fd, ctrl);
    uint64_t mdts = ctrl->mdts;

    return (1 << (mdts - 1) * mpsmin);
    */
}
}