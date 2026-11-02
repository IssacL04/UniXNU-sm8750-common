/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "Sm8750SocInfo.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static uint8_t data[0x200000];
static void w32(size_t at,uint32_t v){for(unsigned i=0;i<4;++i)data[at+i]=(uint8_t)(v>>(i*8));}
static void fixture(unsigned cached,uint32_t fc,uint32_t pc)
{
    memset(data,0,sizeof data);w32(192,1);w32(92,12u<<16);
    const size_t toc=sizeof(data)-4096,p=4096;
    w32(toc,0x434f5424);w32(toc+4,1);w32(toc+8,1);
    w32(toc+32,p);w32(toc+36,4096);w32(toc+44,0xfffefffe);w32(toc+48,64);
    w32(p,0x54525024);w32(p+4,0xfffefffe);w32(p+8,4096);
    size_t h=cached?p+4096-64:p+32,object=cached?h-192:h+16;
    w32(p+12,cached?32:32+16+192);w32(p+16,cached?4096-64-192:4096);
    w32(h,(137u<<16)|0xa5a5);w32(h+4,192);w32(h+8,8);
    w32(object,22);w32(object+4,618);w32(object+176,fc);w32(object+180,pc);
}
int main(void)
{
    uint32_t id=0,code=0;
    for(unsigned cached=0;cached<2;++cached) {
        fixture(cached,3,5);
        assert(!Sm8750SocInfoGpuCode(data,sizeof data,&id,&code) && id==618 && code==3);
        fixture(cached,0xf1,5);
        assert(!Sm8750SocInfoGpuCode(data,sizeof data,&id,&code) && code==0x500f1);
        /* Only the global partition and TOC are readable; a whole-region copy faults. */
        long page=sysconf(_SC_PAGESIZE);assert(page>0 && (size_t)page<sizeof data/2);
        size_t head=(8192+(size_t)page-1)/(size_t)page*(size_t)page;
        uint8_t *mapped=mmap(0,sizeof data,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(mapped!=MAP_FAILED);memcpy(mapped,data,sizeof data);
        assert(!mprotect(mapped+head,sizeof data-head-(size_t)page,PROT_NONE));
        assert(!Sm8750SocInfoGpuCode(mapped,sizeof data,&id,&code) && id==618 && code==0x500f1);
        assert(!munmap(mapped,sizeof data));
    }
    fixture(0,1,0);w32(4096+32+4,0xffffffff);code=99;
    assert(Sm8750SocInfoGpuCode(data,sizeof data,&id,&code) && code==99);
    fixture(0,1,0);w32(sizeof(data)-4096+8,85);
    assert(Sm8750SocInfoGpuCode(data,sizeof data,&id,&code));
    fixture(1,1,0);w32(4096+16,4095);
    assert(Sm8750SocInfoGpuCode(data,sizeof data,&id,&code));
    fixture(0,1,0);w32(4096+32,0);
    assert(Sm8750SocInfoGpuCode(data,sizeof data,&id,&code));
    puts("PASS: SMEM global cached/uncached socinfo, inaccessible partitions, internal-SKU pcode, bounded malformed records");
}
