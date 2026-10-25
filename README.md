# SM8750 common support

Shared GICv3, 19.2 MHz timer, UFS/GCC and SMMU resources used by the OnePlus 13T PKX110 port. The initial extraction preserves the register operations from UniXNU-Pagani-Drivers `0d0b413de6b2caecf2f3128bb70f345342190bcd`.

`PeripheralIO.h` adds bounded SPMI v7 observer reads, QUP1 S4 clock adoption and DSI command-FIFO writes. Board pin numbers, PMIC peripherals, panel limits and physical mappings remain in the device profile. The portable peripheral tests exercise these operations with register models; no new hardware result is claimed. Clock adoption requires boot firmware microcode and supply state, and DSI commands preserve existing scanout timing.

Hardware addresses originate from the device's captured Android live DTB and the register definitions pinned in `soc.toml`. Only CPU affinity 0 is described; other affinities and SMP remain unverified. UFS and SMMU operations retain their C interfaces. Board partition names, memory windows, display and firmware policy remain in the device repository.

Select this repository as `soc:sm8750` and `Sm8750Platform` in the main catalog. From the main checkout run `./unixnu defconfig oneplus13t_defconfig -D ../UniXNU-Pagani_SM8750`, `./unixnu fetch`, `./unixnu tree`, then the device README's checks and build commands. Use `--local-module soc:sm8750=../UniXNU-sm8750-common` while developing.

The original implementation reached the iPadOS UI on OnePlus 13T with build 39. This extracted profile needs a fresh hardware boot; it does not establish support for another SM8750 board. See the [device record](https://github.com/IssacL04/UniXNU-Pagani_SM8750/blob/main/porting.toml) and [SoC guide](https://unixnu.com/porting/soc-support).
