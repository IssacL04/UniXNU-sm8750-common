/* SPDX-License-Identifier: MIT */
#include "PaganiGpuVotes.h"
#include "Sm8750GpuPower.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t db[0x20000];
static void put16(size_t at,unsigned v){db[at]=(uint8_t)v;db[at+1]=(uint8_t)(v>>8);}
static void put32(size_t at,uint32_t v){put16(at,v);put16(at+2,v>>16);}
static void database(void)
{
    memset(db,0,sizeof(db));put32(0,1);put32(4,0x0c0330db);
    put16(8,3);put16(10,0);put16(12,4096-144);put16(14,5);
    put16(24,5);put16(26,120);put16(28,4096-144);put16(30,4);
    const char *names[]={"gfx.lvl","mx.lvl","cx.lvl","gmxc.lvl","gbx.lvl","SH0","MC0","ACV","CN0"};
    const unsigned levels[]={0,32,48,50,52,56,60,64,80,128,144,192,224,256,320,416,448,452};
    for(unsigned i=0;i<9;++i) {
        size_t at=144+i*24,aux=4096+i*64;
        memcpy(db+at,names[i],strlen(names[i]));put32(at+16,(i<5?0x30000:0x50000)+i*16);
        put16(at+20,i<5?sizeof(levels)/sizeof(*levels)*2:i<8?8:0);put16(at+22,i*64);
        if(i<5)for(unsigned j=0;j<sizeof(levels)/sizeof(*levels);++j)put16(aux+j*2,levels[j]);
        else {put32(aux,1000);put16(aux+4,16);db[aux+6]=(uint8_t)(i-5);}
    }
}
int main(void)
{
    uint8_t p[]={0,0,50,0,64,0},s[]={0,0,52,0,60,0,80,0},b[]={0,0,32,0,96,0};
    AdrenoHfiRail primary={p,sizeof(p)},secondary={s,sizeof(s)},bx={b,sizeof(b)};
    uint32_t levels[]={0,51},votes[2]={0},deps[2]={0};
    assert(!AdrenoHfiGen8Votes(levels,2,&primary,&secondary,&primary,&bx,votes,deps,2));
    assert(votes[1]==0x00400302 && deps[0]==UINT32_MAX);
    assert(deps[1]==0x0c2fc2ff);
    uint32_t before[2];memcpy(before,votes,sizeof(votes));levels[1]=65;
    assert(AdrenoHfiGen8Votes(levels,2,&primary,&secondary,&primary,&bx,votes,deps,2));
    assert(!memcmp(before,votes,sizeof(votes)));
    database();Sm8750GmuVoteInputs in;
    assert(!Sm8750GmuCommandDbInputs(db,sizeof(db),&in));
    struct rpmh_command commands[2];
    assert(!Sm8750GpuRailCommands(&in,64,128,commands));
    assert(commands[0].address==0x30010 && commands[0].value==9);
    assert(commands[1].address==0x30020 && commands[1].value==7);
    struct rpmh_command unchanged[2];memcpy(unchanged,commands,sizeof(commands));
    assert(Sm8750GpuRailCommands(&in,64,999,commands)==-22 && !memcmp(commands,unchanged,sizeof(commands)));
    const uint32_t bins[]={190,221,232,242,252,0};
    const uint64_t maxhz[]={900000000,1050000000,1100000000,1150000000,1200000000,1200000000};
    for(unsigned i=0;i<6;++i) {
        AdrenoHfiStartupConfig c;unsigned initial=0,bw=0;
        assert(!PaganiGpuBuildStartup(&in,bins[i],3,&c,&initial,&bw));
        assert(c.gpu[c.gpu_count-1].hz==maxhz[i] && c.performance_table==AdrenoHfiPerfA8xx);
        assert(c.gpu_dependency_votes[0]==UINT32_MAX && c.gmu[1].hz==500000000 && c.gmu[2].hz==650000000);
        assert(initial>0 && initial<c.gpu_count && bw>0 && bw<c.bandwidth_count);
        assert(c.gpu[initial].hz==525000000 && bw==4);
        assert(c.gpu[initial].vote==0x00500808u);
        assert(!AdrenoHfiStartupValidate(&c));
        uint32_t wire[159],id;size_t count;
        assert(!AdrenoHfiStartupEncode(&c,AdrenoHfiStagePerformance,&id,wire,159,&count));
        assert(id==15 && count==6+3*c.gpu_count+2*c.gmu_count && wire[5]==UINT32_MAX);
    }
    AdrenoHfiStartupConfig c;unsigned initial,bw;
    assert(!PaganiGpuBuildStartup(&in,252,1,&c,&initial,&bw) && c.gpu[c.gpu_count-1].hz==1100000000);
    Sm8750GmuVoteInputs saved=in;
    put32(0,2);assert(Sm8750GmuCommandDbInputs(db,sizeof(db),&in)==Sm8750GmuDbVersion && !memcmp(&in,&saved,sizeof(in)));
    database();memcpy(db+168,"gfx.lvl",8);assert(Sm8750GmuCommandDbInputs(db,sizeof(db),&in)==Sm8750GmuDbDuplicate);
    database();put16(14,5000);assert(Sm8750GmuCommandDbInputs(db,sizeof(db),&in)==Sm8750GmuDbCorrupt);
    database();put16(144+20,0xffff);put16(144+22,0xffff);assert(Sm8750GmuCommandDbInputs(db,sizeof(db),&in)==Sm8750GmuDbCorrupt);
    database();db[144]='x';assert(Sm8750GmuCommandDbInputs(db,sizeof(db),&in)==Sm8750GmuDbMissing);
    database();db[144+3*24]='x';db[144+4*24]='x';
    assert(!Sm8750GmuCommandDbInputs(db,sizeof(db),&in) && !in.gmxc.data && !in.gbx.data);
    assert(!PaganiGpuBuildStartup(&in,190,0,&c,&initial,&bw));
    assert(c.gpu_dependency_votes[1]==((0x3fffu<<14)|(3u<<8)|0xffu));
    puts("PASS: Gen8 selected-corner and dependency votes, six DT policies, HFI wire, optional rails and malformed command-db; software inputs only");
}
