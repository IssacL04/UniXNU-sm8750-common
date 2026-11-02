// SPDX-License-Identifier: BSD-2-Clause-Patent
#include "../GpuProbe.h"
#include <assert.h>
#include <stdio.h>

static uint32_t cc[GPU_CC_COUNT], idr0, fail_address;
static unsigned reads, smmu_reads;

static int read32(void *context, const char *name, uint32_t address, uint32_t *out)
{
    (void)context;
    assert(name && !(address & 3));
    ++reads;
    if (address == fail_address) return -1;
    if (address >= SM8750_GPU_SMMU_BASE) {
        assert(smmu_reads < GPU_SMMU_COUNT);
        assert(address == SM8750_GPU_SMMU_BASE + sm8750_gpu_smmu_registers[smmu_reads].offset);
        *out = smmu_reads++ ? 0 : idr0;
        return 0;
    }
    for (unsigned i = 0; i < GPU_CC_COUNT; ++i) {
        if (address == SM8750_GPUCC_BASE + sm8750_gpu_cc_registers[i].offset) {
            *out = cc[i];
            return 0;
        }
    }
    assert(0 && "access outside the read-only register list");
    return -1;
}

static enum sm8750_gpu_probe_result run(struct sm8750_gpu_snapshot *s)
{
    reads = smmu_reads = 0;
    return sm8750_gpu_probe(read32, NULL, s);
}

int main(void)
{
    struct sm8750_gpu_snapshot s;
    assert(sm8750_gpu_probe(NULL, NULL, &s) == GPU_PROBE_INVALID);
    assert(sm8750_gpu_probe(read32, NULL, NULL) == GPU_PROBE_INVALID);
    // Enumerate power, collapse, reset, both votes and both halted bits.
    for (unsigned bits = 0; bits < 128; ++bits) {
        cc[GPU_CX_POWER] = ((bits & 1) ? 0x80000000u : 0) | ((bits >> 1) & 1);
        cc[GPU_CX_RESET] = (bits >> 2) & 1;
        cc[GPU_AHB_CLOCK] = ((bits >> 3) & 1) | ((bits & 16) ? 0x80000000u : 0);
        cc[GPU_SMMU_CLOCK] = ((bits >> 5) & 1) | ((bits & 64) ? 0x80000000u : 0);
        idr0 = 0x400;
        int ready = bits == (1 | 8 | 32);
        assert(run(&s) == (ready ? GPU_PROBE_OK : GPU_PROBE_SMMU_SKIPPED));
        assert(smmu_reads == (ready ? GPU_SMMU_COUNT : 0));
    }
    cc[GPU_CX_POWER] = 0x80000000u;
    cc[GPU_CX_RESET] = 0;
    cc[GPU_AHB_CLOCK] = cc[GPU_SMMU_CLOCK] = 1;
    for (unsigned i = 0; i < 4; ++i) {
        uint32_t saved = cc[i];
        cc[i] = UINT32_MAX;
        assert(run(&s) == GPU_PROBE_SMMU_SKIPPED && !smmu_reads);
        cc[i] = saved;
    }
    for (unsigned i = 0; i < GPU_CC_COUNT + GPU_SMMU_COUNT; ++i) {
        fail_address = i < GPU_CC_COUNT ? SM8750_GPUCC_BASE + sm8750_gpu_cc_registers[i].offset :
            SM8750_GPU_SMMU_BASE + sm8750_gpu_smmu_registers[i - GPU_CC_COUNT].offset;
        assert(run(&s) == GPU_PROBE_READ_FAILED);
        assert(reads == i + 1);
    }
    fail_address = 0;
    idr0 = 0;
    assert(run(&s) == GPU_PROBE_INVALID && smmu_reads == 1);
    idr0 = UINT32_MAX;
    assert(run(&s) == GPU_PROBE_INVALID && smmu_reads == 1);
    idr0 = 0x400;
    assert(run(&s) == GPU_PROBE_OK && s.smmu_count == GPU_SMMU_COUNT);
    puts("SM8750 GPU probe: 128 power/clock states, read failures and invalid IDs PASS");
}
