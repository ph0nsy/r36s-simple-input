# Simple Input for R36S
![License](https://img.shields.io/badge/License-GPL-lightgrey?style=for-the-badge)
![Rockchip](https://img.shields.io/badge/Rockchip-RK3326-blue?style=for-the-badge)
![ArkOS' kernel](https://img.shields.io/badge/ArkOS'%20kernel-4.4.189-yellowgreen?style=for-the-badge)

A Linux kernel module for the R36S handheld that reads the device's D-pad, 
face buttons, shoulder buttons, and volume keys directly from GPIO hardware 
registers, and exposes them to userspace as a single packed bitmask, alongside 
the stock `gpio-keys` / `gpio-keys-polled` drivers already running on the 
device, not in place of them.

This exists as a learning project in embedded Linux driver development: 
kernel/toolchain identification, cross-compilation, device tree archaeology, 
and direct _MMIO register_ access, built and verified end to end against 
real hardware. 

>See [the full writeup](https://sackonline.org/wp-content/uploads/2020/01/under-construction-meme.jpg) for the complete process, including the dead ends.

> ❗ **Important**
>
> _ArkOS already ships working input drivers for this board. This module 
> doesn't replace them._
> 
> It's a read-only "shadow" driver: it maps the same 
> physical _GPIO bank registers_ the stock drivers already read, and reports 
> the same button states through its own character device, `/dev/r36s_simple_input`, 
> as one 32-bit semantic bitmask instead of the standard Linux input event stream.

## Features

- Reads 20 physical buttons across all three of the board's _GPIO_ banks in a single `read()` call.
- Packs them into one semantic bitmask, hiding which physical pin or bank each button actually lives on.
- Coexists safely with the stock input drivers already running (read-only, no shared resource conflicts).
- Verified bit-by-bit against physical button presses, cross-checked with `evtest`.

### Bit layout

A value of `1` in any bit means that bit it's been pressed. 
 
| Bit | Button |
|---|---|
| 0 | D-pad Up || 14 | TR |
| 1 | D-pad Down | 8 | Vol Up | 15 | F6 *(unpopulated)* |
| 2 | D-pad Left | 9 | Vol Down | 16 | F1 / Select |
| 3 | D-pad Right | 10 | F3 / TL3 (L-stick click) | 17 | TL2 |
| 4 | A | 11 | F4 / TR3 (R-stick click) | 18 | F2 / Start |
| 5 | B | 12 | F5 / FN | 19 | TR2 |
| 6 | Y | 13 | *(reserved)* | | |
| 7 | X |
| 8 | Vol Up |
| 9 | Vol Down |
| 10 | F3 / TL3 (L-stick click) |
| 11 | F4 / TR3 (R-stick click) |
| 12 | F5 / FN |
| 13 | *Reserved* |
| 14 | TL |
| 15 | TR |
| 16 | F1 / SELECT |
| 17 | TL2 |
| 18 | F2 / START |
| 19 | TR2 |

> ❗ **Important**
>
> Analog stick axes are **not** included. See [Known limitations](#known-limitations).

## Requirements

- An R36S or other _RK3326_-family _ArkOS_ device running kernel `4.4.189`.
	- Check with `uname -a` on the device. This module is built against that exact kernel and will not load on a mismatched `vermagic`.
- A build machine (any _x86_64 Ubuntu_ `18.04–22.04` is a safe bet) with:
	- The `gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu` cross toolchain
	- A configured build of the matching kernel source ([`christianhaitian/linux`](https://github.com/christianhaitian/linux), branch `rg351`, `rg351p_tweaked_defconfig`).

## Quickstart
### 1. Build the kernel source once

```bash
git clone -b rg351 https://github.com/christianhaitian/linux.git
cd linux
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make rg351p_tweaked_defconfig
make -j$(nproc) Image modules dtbs
```

Provides the configured tree this module builds against.

> ❗ **Important**
>
> Check that `cat include/config/kernel.release` prints `4.4.189`. 
> If it doesn't match your device's `uname -a`, stop here and resolve that first.

### 2. Build this module

```bash
git clone https://github.com/YOUR_USERNAME/r36s-simple-input.git
cd r36s-simple-input
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-
make KDIR=/path/to/the/linux/tree/from/step/1
```

### 3. Install

First: 

```bash
scp simple_input.ko ark@YOUR_DEVICE_IP:~/
```

Then access your device via ssh:
- On the R36S:
	- Set up WiFi 
	- Enable Remote Services
- Connect with _Putty_ (or a similar tool) from your device to your R36S.

Lastly: 

```bash
sudo insmod simple_input.ko
```

Check `dmesg | tail` for a `LOADED` line to confirm it's running.

### 4. Usage

```bash
cat /dev/r36s_simple_input | xxd
```

Returns 4 bytes: a little-endian `uint32_t` bitmask, laid out per the [bit table](#bit-layout) above. Press a button and re-run to see the corresponding bit set.

### From a C program

This is an example once the previous have been done:

```c
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define R36S_INPUT_DEV "/dev/r36s_simple_input"

/* bit positions, matching the table in this README */
#define BTN_DPAD_UP    (1u << 0)
#define BTN_A          (1u << 4)

int main(void)
{
    int fd = open(R36S_INPUT_DEV, O_RDONLY);
    if (fd < 0) 
    {
        perror("open " R36S_INPUT_DEV);
        return 1;
    }

    while (true) {
        uint32_t buttons = 0;

        /* 
         * lseek back to 0 each time: the driver's own *ppos tracking
         * treats a single read() as "the data", not a growing stream,
         * so re-reading needs a fresh offset, not a continued one 
         */
        if (lseek(fd, 0, SEEK_SET) < 0) 
        {
            perror("lseek");
            break;
        }

        ssize_t n = read(fd, &buttons, sizeof(buttons));
        if (n != sizeof(buttons)) 
        {
            fprintf(stderr, "short read: got %zd bytes, expected %zu\n", n, sizeof(buttons));
            break;
        }

        if (buttons & BTN_A) { printf("A pressed\n"); }
        if (buttons & BTN_DPAD_UP) { printf("D-pad up pressed\n"); }

        /*
         * poll again after a short sleep, or however often your
         * application's own loop needs a fresh read 
         */
        usleep(20000);
    }

    close(fd);
    return 0;
}
```

A couple clarifications:

- **Re-read from offset 0 every time.** The driver's `read()` implements simple one-shot EOF semantics (it returns `0` on a second `read()` at the same file offset, matching how `cat` expects a device file to behave). A long-running consumer needs to `lseek(fd, 0, SEEK_SET)` before each subsequent `read()`, not just call `read()` in a loop expecting new data automatically.
- **This is a poll-on-demand interface, not a blocking event stream.** There's no `select()`/`poll()` support and no notification when a button changes, each `read()` is a fresh, live snapshot of hardware state at that exact instant. See [Known limitations](#known-limitations) for what that means for very short button presses.

To unload:

```bash
sudo rmmod simple_input
```

## Known limitations

- **No analog stick support.** The sticks share a single _ADC_ channel through an analog multiplexer, both actively controlled (_GPIO_ writes, register writes) by the stock `odroidgo3-joypad` driver. Reading them safely alongside that driver would require either replacing it outright or real cross-driver coordination, both out of scope here. This is a deliberate, evidenced architectural decision, not an oversight.
- **Polling-based, not interrupt-driven**, in the sense that each `read()` is a fresh, live register read rather than a cached, continuously-updated value. A button press shorter than the gap between two reads can be missed entirely. The stock drivers don't have this limitation, since they're interrupt- or timer-driven independent of when userspace happens to read.
- **Kernel/board specific.** The _GPIO_ base addresses and bit positions are hardcoded for this exact board's device tree (_RK3326/PX30_, `rg351mp` variant). Porting to a different board in this family would need the addresses re-derived from that board's own `.dts`.

## Acknowledgments

- [ArkOS](https://github.com/christianhaitian/arkos) and the [`christianhaitian/linux`](https://github.com/christianhaitian/linux) kernel fork this module builds against and reads state alongside
- The Rockchip `pinctrl-rockchip.c` and `rockchip_saradc.c` kernel drivers, the closest thing to a datasheet available for this SoC

## Author

Alonso Moreno — [<img src="https://custom-icon-badges.demolab.com/badge/LinkedIn-0A66C2?logo=linkedin-white&logoColor=fff" alt="LinkedIn" style="vertical-align: middle;"/>](https://github.com/ph0nsy) [<img src="https://img.shields.io/badge/GitHub-%23121011.svg?logo=github&logoColor=white" alt="GitHub" style="vertical-align: middle;"/>](https://github.com/ph0nsy)
