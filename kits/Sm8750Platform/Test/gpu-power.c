/* SPDX-License-Identifier: MIT */
#include "Sm8750GpuPower.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t gcc[0x80000/4],cc[0x9800/4];
static unsigned reads,writes,delays,fail_read,fail_write,stuck,rail_calls;
static unsigned cxo_no_latch,cxo_reads,cxo_writes;
static uint32_t demet_status;
static int rail_error;
static uint32_t *reg(unsigned w,uint32_t at)
{assert(w<=1 && !(at&3) && at<(w?sizeof cc:sizeof gcc));return (w?cc:gcc)+at/4;}
static int rd(void *unused,unsigned w,uint32_t at,uint32_t *v)
{(void)unused;if(w==1 && at==0x9008)++cxo_reads;if(++reads==fail_read)return -1;*v=*reg(w,at);return 0;}
static int wr(void *unused,unsigned w,uint32_t at,uint32_t v)
{
    (void)unused;if(++writes==fail_write)return -1;
    *reg(w,at)=v;
    if(w==1 && at==0x9008) {++cxo_writes;if(cxo_no_latch)*reg(w,at)&=~1u;}
    if(w==1 && at==0x9080 && !(v&1) && stuck!=1)cc[0x9094/4]=1u<<31;
    if(w==1 && at==4 && v==1 && stuck!=2)cc[0]|=1u<<31;
    if(w==1 && (at==0x9318 || at==0x93ec) && stuck!=3)*reg(w,at)&=~1u;
    if(w==1 && at==0x90d4 && stuck==4)*reg(w,at)|=1u<<31;
    if(w==1 && at==0x9010 && demet_status)*reg(w,at)=demet_status;
    if(w==1 && at==0x9010 && stuck==5)*reg(w,at)=0x80000000;
    return 0;
}
static int delay(void *unused,unsigned us){(void)unused;delays+=us;assert(delays<20000);return 0;}
static int rails(void *unused,uint32_t cx,uint32_t mx)
{(void)unused;++rail_calls;assert(cx==64 && mx==64 && writes==0);return rail_error;}
static void reset(void)
{
    memset(gcc,0,sizeof gcc);memset(cc,0,sizeof cc);
    gcc[0]=1u<<31;gcc[0x10/4]=31;gcc[0x14/4]=0x4000;gcc[0x52000/4]=0x80;
    cc[0x9080/4]=1;
    reads=writes=delays=fail_read=fail_write=stuck=rail_calls=0;rail_error=0;
    cxo_no_latch=cxo_reads=cxo_writes=0;
    demet_status=0;
}
int main(void)
{
    const Sm8750GpuPowerIo io={0,rd,wr,delay,rails};Sm8750GpuPowerResult r={0};
    reset();assert(!Sm8750GpuPowerOn(&io,&r));
    assert(r.ready && r.stage==7 && rail_calls==1 && gcc[0x52000/4]==0x18080);
    assert(cc[0x10/4]==0x00440034 && cc[0x14/4]==0x1555 && cc[0x18/4]==0x407);
    assert(cc[0x931c/4]==0x201 && cc[0x93f0/4]==0x505 && cc[0x7000/4]==1);
    assert(cc[0x9080/4]==0x00228800 && cc[0]==0x80000005);
    const unsigned total_reads=reads,total_writes=writes;
    assert(Sm8750GpuPowerOn(&io,&r)==-16 && reads==total_reads && writes==total_writes);
    for(unsigned i=1;i<=total_reads;++i) {
        reset();fail_read=i;r=(Sm8750GpuPowerResult){0};
        assert(Sm8750GpuPowerOn(&io,&r)==-5 && !r.ready && reads==i && r.retain_resources);
    }
    for(unsigned i=1;i<=total_writes;++i) {
        reset();fail_write=i;r=(Sm8750GpuPowerResult){0};
        assert(Sm8750GpuPowerOn(&io,&r)==-5 && !r.ready && writes==i && r.retain_resources);
    }
    for(unsigned i=1;i<=5;++i) {
        reset();stuck=i;r=(Sm8750GpuPowerResult){0};
        assert(Sm8750GpuPowerOn(&io,&r)==-110 && !r.ready && r.retain_resources);
    }
    reset();rail_error=-16;r=(Sm8750GpuPowerResult){0};
    assert(Sm8750GpuPowerOn(&io,&r)==-16 && !reads && !writes);
    reset();cc[0]=0x80000005;cc[4/4]=1;cc[0x10/4]=0x34;cc[0x14/4]=0x1555;cc[0x18/4]=0x407;
    r=(Sm8750GpuPowerResult){0};assert(!Sm8750GpuPowerOn(&io,&r));
    reset();cc[0]=0x80000005;cc[4/4]=1;cc[0x10/4]=0x43;
    r=(Sm8750GpuPowerResult){0};assert(Sm8750GpuPowerOn(&io,&r)==-16 && cc[0x10/4]==0x43);
    reset();gcc[0x14/4]=0;r=(Sm8750GpuPowerResult){0};
    assert(Sm8750GpuPowerOn(&io,&r)==-95 && !cc[0x10/4]);
    reset();cc[0x907c/4]=1;r=(Sm8750GpuPowerResult){0};
    assert(Sm8750GpuPowerOn(&io,&r)==-16 && cc[0x9080/4]==1);
    reset();cxo_no_latch=1;cc[0x9008/4]=0x08000000;r=(Sm8750GpuPowerResult){0};
    assert(!Sm8750GpuPowerOn(&io,&r) && r.ready && r.stage==7);
    assert(cc[0x9008/4]==0x08000000 && cxo_reads==1 && cxo_writes==1);
    const uint32_t status[]={0x08000000,0xa0000000,0x80000000,0x90000000,0xb0000000};
    for(unsigned i=0;i<sizeof(status)/sizeof(*status);++i) {
        reset();demet_status=status[i];r=(Sm8750GpuPowerResult){0};
        int rc=Sm8750GpuPowerOn(&io,&r);
        if(i<2)assert(!rc && r.ready && r.stage==7);
        else assert(rc==-110 && !r.ready && r.stage==6 && r.window==1 &&
                    r.offset==0x9010 && r.value==status[i]);
        assert(cc[0x9010/4]==status[i]);
    }
    printf("PASS: SM8750 clock/power software sequence, %u read and %u write failures, five bounded stalls, vendor critical-clock writes and branch HALT/FSM status, live-rate/reset refusals\n",total_reads,total_writes);
}
