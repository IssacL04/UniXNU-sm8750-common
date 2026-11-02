/* SPDX-License-Identifier: MIT */
#include "Sm8750GpuZap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static _Alignas(4096) uint8_t cpu[0x4000],reserved[0x2000];
static uint8_t metadata[0xfcc],payload[0x1000];
static struct {unsigned step,fail,published,mode,calls;uint64_t now;} peer;
static int now(void *p,uint64_t *out){(void)p;*out=peer.now++;return 0;}
static int wait(void *p,unsigned us){(void)p;peer.now+=us;return 0;}
static int publish(void *p,const void *data,size_t bytes)
{
    (void)p;
    if(!peer.published)assert(data==cpu && bytes==sizeof(cpu) && !memcmp(cpu,metadata,sizeof(metadata)));
    else assert(data==reserved && bytes==sizeof(payload) && !memcmp(reserved,payload,sizeof(payload)));
    ++peer.published;return 0;
}
static int invoke(void *p,uint64_t r[8])
{
    (void)p;++peer.calls;
    if(peer.mode==4) {
        assert(r[0]==0x42002801 && r[1]==1 && r[2]==9 && !r[3] && !r[4]);
        r[0]=0;r[1]=0;return 0;
    }
    if(peer.mode==1 && peer.calls==1) {r[0]=1;r[6]=0x1234;return 0;}
    if(peer.mode==1)assert(r[0]==1 && r[6]==0x1234);
    if(peer.mode==2) {r[0]=(uint64_t)-12;return 0;}
    if(peer.mode==3) {r[0]=2;return 0;}
    const uint32_t functions[]={0x42000201,0x42000202,0x42000205};
    assert(peer.step<3 && (peer.mode==1 || r[0]==functions[peer.step]));
    assert(r[2]==13);
    if(peer.step==0)assert(peer.published==1 && r[1]==0x82 && r[3]==0xa0000000);
    if(peer.step==1)assert(r[1]==3 && r[3]==0x9b09a000 && r[4]==0x1000);
    if(peer.step==2)assert(peer.published==2 && r[1]==1);
    ++peer.step;r[0]=0;r[1]=peer.fail==peer.step?1:0;return 0;
}
int main(void)
{
    const Sm8750ScmIo io={0,invoke,now,wait,publish};
    memset(metadata,0x55,sizeof(metadata));memset(payload,0x77,sizeof(payload));
    for(unsigned fail=0;fail<4;++fail) {
        memset(&peer,0,sizeof(peer));peer.fail=fail;memset(reserved,0xa5,sizeof(reserved));
        Sm8750GpuZapResult out;
        assert((Sm8750GpuZapLoad(&io,metadata,sizeof(metadata),payload,sizeof(payload),cpu,0xa0000000,reserved,&out)==0)==!fail);
        assert(out.authenticated==!fail && peer.step==(fail?fail:3));
        if(fail==1 || fail==2)assert(reserved[0]==0xa5);
        assert(reserved[0x1000]==0xa5);
    }
    Sm8750ScmResult result;
    memset(&peer,0,sizeof(peer));peer.mode=4;
    assert(!Sm8750GpuSecureCxInit(&io,&result) && result.calls==1);
    Sm8750GpuZapResult invalid;
    assert(Sm8750GpuZapLoad(&io,metadata,sizeof(metadata),payload,sizeof(payload),cpu,0x9b098000,reserved,&invalid)==-22);
    assert(peer.calls==1);
    memset(&peer,0,sizeof(peer));peer.mode=1;peer.published=1;
    assert(!Sm8750ScmCall(&io,0x42000201,0x82,13,0xa0000000,0,&result) && result.calls==2);
    memset(&peer,0,sizeof(peer));peer.mode=2;
    assert(Sm8750ScmCall(&io,0x42000201,0x82,13,0xa0000000,0,&result) && result.calls==21);
    memset(&peer,0,sizeof(peer));peer.mode=3;
    assert(Sm8750ScmCall(&io,0x42000201,0x82,13,0xa0000000,0,&result) && result.calls==1);
    puts("PASS: PAS ordering, signed metadata publication, authentication refusals, bounded busy/resume and WAITQ refusal; software SMC peer only");
}
