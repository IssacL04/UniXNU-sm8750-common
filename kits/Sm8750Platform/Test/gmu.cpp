// SPDX-License-Identifier: BSD-2-Clause-Patent
// Real loader and local firmware bytes, software MMIO peer only.
#include "Sm8750Gmu.h"
#include "Sm8750GmuSource.h"
#include "NativeAdrenoSha256.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

struct Peer {
    uint32_t regs[3][0x68000/4]{};
    uint64_t time=0;
    unsigned reads=0,writes=0,fail_read=0,fail_write=0;
    bool ready=true,bad_readback=false,bad_clock=false,fail_wait=false;
    unsigned commands=0,reject_command=0;
    bool reply=false,oob_reply=true,stale_ack=false;
};
static Peer peer;
static Sm8750GmuConfig config;
static Sm8750GmuDevice device;
static size_t offset(uint32_t word) {return word*4-SM8750_GMU_CORE_GPU_DELTA;}
static uint32_t &reg(uint32_t word) {return peer.regs[Sm8750GmuCore][offset(word)/4];}
static int read32(void *cookie,unsigned window,size_t at,uint32_t *value)
{
    assert(cookie==&peer && window<3 && at+4<=sm8750_gmu_window_bytes[window] && !(at&3));
    if(++peer.reads==peer.fail_read)return 1;
    *value=peer.regs[window][at/4];
    if(peer.bad_readback && window==Sm8750GmuCore && at==offset(REG_GMU_CM3_CFG))*value^=1;
    return 0;
}
static int write32(void *cookie,unsigned window,size_t at,uint32_t value)
{
    assert(cookie==&peer && window<3 && at+4<=sm8750_gmu_window_bytes[window] && !(at&3));
    if(++peer.writes==peer.fail_write)return 1;
    peer.regs[window][at/4]=value;
    if(peer.ready && window==Sm8750GmuCore) {
        if(at==offset(REG_GMU_GMU2HOST_INTR_CLR) && !peer.stale_ack)reg(REG_GMU_GMU2HOST_INTR_INFO)&=~value;
        if(at==offset(REG_GMU_HOST2GMU_INTR_SET) && value==(1u<<30) && peer.oob_reply)
            reg(REG_GMU_GMU2HOST_INTR_INFO)|=1u<<31;
        if(at==offset(REG_GMU_CM3_SYSRESET) && !value) {
            const uint32_t version=reg(REG_GMU_CM3_DTCM_START+0xff8);
            reg(REG_GMU_CM3_FW_INIT_RESULT)=version<=0x20010004?0xbabeface:0x100;
        }
        if(at==offset(REG_GMU_HFI_CTRL_INIT) && value==1)reg(REG_GMU_HFI_CTRL_STATUS)=1;
        if(at==offset(REG_GMU_HOST2GMU_INTR_SET) && value==1) {
            volatile uint32_t *shared=config.hfi.cpu;
            const uint32_t command=shared[1024+shared[16]];
            shared[16]=shared[17];++peer.commands;
            if(peer.reply) {
                const unsigned write=shared[29];
                shared[2048+write]=(3u<<8)|126u;
                shared[2048+((write+1)&1023)]=command;
                shared[2048+((write+2)&1023)]=peer.commands==peer.reject_command?37:0;
                shared[2048+((write+3)&1023)]=0xfafafafa;
                shared[29]=(write+4)&1023;
            }
        }
    }
    return 0;
}
static int barrier(void *cookie) {assert(cookie==&peer);return 0;}
static int hash(void *,const void *bytes,size_t length,uint8_t digest[32])
{NativeAdrenoSha256::hash(static_cast<const uint8_t *>(bytes),length,digest);return 0;}
static int now(void *,uint64_t *value)
{if(peer.bad_clock)return 1;*value=peer.time;return 0;}
static int wait(void *,unsigned us)
{if(peer.fail_wait)return 1;peer.time+=us;return 0;}
static void reset()
{
    peer=Peer{};device={};
    reg(REG_GMU_CM3_SYSRESET)=1;
    device.io.cookie=&peer;device.io.read32=read32;device.io.write32=write32;device.io.barrier=barrier;
    for(unsigned i=0;i<3;++i)device.io.bytes[i]=sm8750_gmu_window_bytes[i];
    device.services={&peer,hash,now,wait};
}
static Sm8750GmuMemory memory(size_t bytes,uint64_t iova)
{
    void *p=std::aligned_alloc(4096,bytes);assert(p);
    std::memset(p,0xa5,bytes);
    return {static_cast<volatile uint32_t *>(p),bytes,iova};
}
int main(int argc,char **argv)
{
    static_assert(REG_GMU_GMU2HOST_INTR_CLR==0x1f991,"Gen8 clear register, not interrupt mask");
    assert(argc==2);
    std::ifstream input(argv[1],std::ios::binary);assert(input);
    std::vector<uint8_t> firmware{std::istreambuf_iterator<char>(input),{}};
    config={SM8750_GMU_CHIP_ID,firmware.data(),firmware.size(),
        memory(0xffc000,0x4000),memory(0x4000,0x60000000),memory(0x7000,0x60400000),
        memory(0x4000,0x60100000),memory(0x4000,0x60200000),16};
    Sm8750GmuResult result{};
    reset();
    assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==Sm8750GmuSuccess);
    assert(result.complete_hash_verified && result.phase==7 && result.hfi_ready_observed);
    assert(result.firmware_ready_observed && !result.reset_verified && result.resources_must_retain);
    assert(reg(REG_GMU_GENERAL_10)==0x08000100 && reg(REG_GMU_HFI_QTBL_ADDR)==config.hfi.iova);
    const unsigned writes=peer.writes,reads=peer.reads;
    assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==Sm8750GmuBusy);
    assert(peer.writes==writes);

    // Every injected I/O failure, including TCM transfer/readback, is observable.
    for(unsigned fail=1;fail<=writes;++fail) {
        reset();peer.fail_write=fail;
        assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==Sm8750GmuIoError);
        assert(device.poisoned && device.hfi.poisoned && result.resources_must_retain);
        assert(!result.hfi_ready_observed && result.reset_verified && reg(REG_GMU_CM3_SYSRESET)==1);
    }
    for(unsigned fail=1;fail<=reads;++fail) {
        reset();peer.fail_read=fail;
        assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==Sm8750GmuIoError);
        assert(!result.hfi_ready_observed && result.resources_must_retain);
        if(device.boot_attempted)assert(device.poisoned && result.reset_verified);
        else assert(!peer.writes);
    }
    for(unsigned mode=0;mode<6;++mode) {
        reset();
        if(mode==0)reg(REG_GMU_CM3_SYSRESET)=0;
        if(mode==1)reg(REG_GMU_HFI_CTRL_STATUS)=1;
        if(mode==2)peer.regs[Sm8750GmuRscc][REG_RSCC_SEQ_BUSY_DRV0]=1;
        if(mode==3)reg(REG_GMU_AO_HOST_INTERRUPT_STATUS)=1;
        if(mode==4)reg(REG_GMU_GMU2HOST_INTR_INFO)=1u<<23;
        if(mode==5)firmware[0]^=1;
        const Sm8750GmuStatus expected[]={Sm8750GmuBusy,Sm8750GmuStateMismatch,
            Sm8750GmuBusy,Sm8750GmuFault,Sm8750GmuFault,Sm8750GmuFirmware};
        assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==expected[mode]);
        assert(!peer.writes && !device.boot_attempted);
        if(mode==5)firmware[0]^=1;
    }
    for(unsigned mode=0;mode<4;++mode) {
        reset();peer.ready=false;
        if(mode==1)peer.bad_clock=true;
        if(mode==2)peer.fail_wait=true;
        if(mode==3)peer.bad_readback=true;
        const Sm8750GmuStatus expected[]={Sm8750GmuTimeout,Sm8750GmuClockError,
            Sm8750GmuClockError,Sm8750GmuStateMismatch};
        assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==expected[mode]);
        assert(device.poisoned && result.reset_verified && !result.hfi_ready_observed);
        assert(Sm8750GmuLoadAndStartHfi(&device,&config,&result)==Sm8750GmuPoisoned);
    }
    reset();
    Sm8750GmuConfig invalid=config;invalid.hfi.iova=config.log.iova;
    assert(Sm8750GmuLoadAndStartHfi(&device,&invalid,&result)==Sm8750GmuArgument && !peer.writes);
    invalid=config;invalid.firmware_bytes--;
    assert(Sm8750GmuLoadAndStartHfi(&device,&invalid,&result)==Sm8750GmuArgument && !peer.writes);
    Sm8750GmuStartupResult startup{};
    Sm8750GmuStartupLimits limits{16,100,10000,100000};
    AdrenoHfiStartupConfig wrong_generation{};
    assert(Sm8750GmuColdStart(&device,&config,&wrong_generation,&limits,&startup)==Sm8750GmuArgument);
    assert(!peer.writes);
    // These OPPs and addresses are test fixtures, never board voltage policy.
    AdrenoHfiStartupConfig protocol{};
    protocol.performance_table=AdrenoHfiPerfA8xx;
    protocol.gpu_count=protocol.gmu_count=2;
    protocol.gpu[1]={100000000,1};protocol.gmu[1]={200000000,1};
    protocol.bandwidth_count=protocol.cnoc_count=protocol.ddr_count=1;
    protocol.cnoc_addresses[0]=0x1000;protocol.ddr_addresses[0]=0x2000;
    for(unsigned reject=0;reject<=5;++reject) {
        reset();peer.reply=reject!=5;peer.reject_command=reject;
        const Sm8750GmuStatus expected=reject==0?Sm8750GmuSuccess:
            reject==5?Sm8750GmuTimeout:Sm8750GmuProtocolError;
        assert(Sm8750GmuColdStart(&device,&config,&protocol,&limits,&startup)==expected);
        if(!reject)assert(device.startup.completed && peer.commands==4 && startup.protocol.published_commands==4);
        else assert(device.poisoned && device.startup.poisoned && startup.load.reset_verified);
    }
    // Runtime controls must consume fresh OOB and HFI responses without reset.
    for(unsigned mode=0;mode<5;++mode) {
        reset();peer.reply=true;
        assert(Sm8750GmuColdStart(&device,&config,&protocol,&limits,&startup)==Sm8750GmuSuccess);
        device.bandwidth_levels=3;
        assert(Sm8750GmuSetPerformance(&device,0,1,&result)==Sm8750GmuArgument);
        if(mode==1)peer.oob_reply=false;
        if(mode==2) {peer.stale_ack=true;reg(REG_GMU_GMU2HOST_INTR_INFO)=1u<<31;}
        const auto oob=Sm8750GmuGpuOob(&device,1,&result);
        if(mode==1 || mode==2) {
            assert(oob==(mode==1?Sm8750GmuTimeout:Sm8750GmuStateMismatch));
            assert(device.poisoned && !device.gpu_oob_held && !reg(REG_GMU_CM3_SYSRESET));continue;
        }
        assert(oob==Sm8750GmuSuccess && device.gpu_oob_held);
        assert(Sm8750GmuGpuOob(&device,1,&result)==Sm8750GmuBusy);
        if(mode==3)peer.reply=false;
        if(mode==4)peer.reject_command=peer.commands+1;
        const auto vote=Sm8750GmuSetPerformance(&device,1,2,&result);
        assert(vote==(mode==3?Sm8750GmuTimeout:mode==4?Sm8750GmuProtocolError:Sm8750GmuSuccess));
        if(mode)assert(device.poisoned && device.gpu_oob_held && !reg(REG_GMU_CM3_SYSRESET));
        else assert(Sm8750GmuGpuOob(&device,0,&result)==Sm8750GmuSuccess && !device.gpu_oob_held);
    }
    for(auto m:{config.icache,config.dummy,config.debug,config.log,config.hfi})
        std::free(const_cast<uint32_t *>(m.cpu));
    std::printf("PASS: pinned GMU load/hash/TCM readback, %u write and %u read failures, stale/fatal status, timeout/reset; software MMIO only\n",writes,reads);
}
