# GT911 Touch Controller — ESP32-P4 (Interrupt Mode)

A minimal, working example of the **GT911 capacitive multi touch controller** on the **ESP32-P4** using **ESP-IDF 5.5.x**, driven by hardware interrupts (no polling). Touch coordinates are logged over serial the moment you lift a finger.

If you've been banging your head against this — I2C not detecting, interrupts not firing, queue crashes — read the [Common Pitfalls](#common-pitfalls) section first.

---

## Hardware

| Signal | GPIO |
|--------|------|
| I2C SDA | GPIO 7 |
| I2C SCL | GPIO 8 |
| TOUCH RST | GPIO 22 |
| TOUCH INT | GPIO 21 |

- **I2C address:** `0x5D`
- **I2C speed:** 400 kHz
- **Display resolution:** 480 × 800 (portrait)
- **Target board:** JC4880P443C (ESP32-P4, 16 MB flash, PSRAM)

If your board uses different pins, edit the `#define` block at the top of `main/gt911_touch.c`.

---

## Requirements

| Tool | Version |
|------|---------|
| ESP-IDF | **5.5.2** (see note below) |
| Target chip | ESP32-P4 |
| Component: `espressif/esp_lcd_touch_gt911` | `^1.2.0` |

> ⚠️ **IDF version matters.** This project was developed and tested on **ESP-IDF 5.5.2**. The new `i2c_master` driver API used here (`i2c_new_master_bus`, `i2c_master_probe`, etc.) is **not available in IDF 4.x or early 5.x**. The legacy `i2c_driver_install()` API will not work with this code.

---

## Getting Started

### 1. Install ESP-IDF 5.5.x

Follow the [official guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32p4/get-started/index.html) or use the VS Code ESP-IDF extension.

```bash
. $HOME/esp/esp-idf/export.sh   # or wherever you installed it
```

### 2. Clone / copy this project

```bash
# If you cloned from GitHub:
cd gt911_touch
```


### 3. Build, flash, monitor

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash monitor   # adjust port as needed
```

On Windows:
```bash
	learn to use a proper operating system
```

---

## What You Should See

On boot the firmware probes the I2C bus before doing anything else:

```
I (312) TOUCH_IRQ: === Testing I2C Communication ===
I (318) TOUCH_IRQ: ✓ GT911 detected at address 0x5D
I (324) TOUCH_IRQ: Product ID: 0x39 0x31 0x31
I (328) TOUCH_IRQ: ✓ GT911 confirmed!
I (401) TOUCH_IRQ: ✓ I2C master bus created
I (412) TOUCH_IRQ: ✓ Touch panel IO created
I (489) TOUCH_IRQ: ✓ GT911 touch initialized (SDA=GPIO7, SCL=GPIO8, INT=GPIO21)
I (490) TOUCH_IRQ: ✅ Interrupt mode ready! Touch the screen to see coordinates.
```

Then, each time you touch the screen:

```
I (8341) TOUCH_IRQ: [Touch #1] 1 point(s):
I (8341) TOUCH_IRQ:   Point 0: X= 240, Y= 400, Strength= 42
```

A status report prints every 10 seconds showing total touch count.

---

## Common Pitfalls

These are the things most likely to have you pulling your hair out.

### ❌ GT911 not detected on I2C

The probe at boot will tell you immediately if the chip isn't responding.

**Things to check:**

1. **Pull-up resistors** — SDA and SCL each need a pull-up to 3.3 V (typically 4.7 kΩ). The code enables internal pull-ups (`flags.enable_internal_pullup = true`) but they are weak (~50 kΩ) and may not be enough, especially at 400 kHz. Add external resistors if detection is unreliable.

2. **RST and INT sequencing** — The GT911 requires a specific reset sequence to choose its I2C address. If RST or INT are floating or driven incorrectly during boot, the device may not appear. This code drives RST active-low and reads INT active-low, matching what works on the JC4880P443C board.

3. **Address** — GT911 can appear at `0x5D` or `0x14` depending on the INT pin state during reset. This project uses `0x5D`. If your hardware uses `0x14`, change `gt911_cfg.dev_addr` in `touch_init()`.

4. **Wrong GPIO** — Triple-check your wiring against the `#define` block.

### ❌ Interrupt fires once then stops

This is the most common GT911 gotcha. The INT pin stays asserted until the host reads the touch data. If your ISR doesn't trigger a data read quickly enough, or if you read data without clearing the register, the interrupt line stays low and no new edges are generated.

This project handles it correctly: the ISR queues a notification, and the processing task immediately calls `esp_lcd_touch_read_data()` which clears the interrupt on the chip.

### ❌ Crash / panic in the ISR

The original version of this code used `xQueueSendFromISR(queue, NULL, ...)` — passing `NULL` as the item pointer causes a crash because FreeRTOS tries to copy `sizeof(uint32_t)` bytes from address 0. The fix is to send an actual value:

```c
uint32_t notification = 1;
xQueueSendFromISR(touch_event_queue, &notification, &xHigherPriorityTaskWoken);
```

And the receiving task must provide a matching buffer:

```c
uint32_t notification;
xQueueReceive(touch_event_queue, &notification, portMAX_DELAY);
```

This is already correct in the current code — just don't revert it.

### ❌ Build error: `i2c_new_master_bus` undeclared

You're on the wrong IDF version. The legacy `i2c_driver_install()` API and the new `i2c_master` API are **not interchangeable**. You need IDF 5.1+ for the new driver; 5.5.x is recommended.

### ❌ Coordinates are mirrored or swapped

Adjust the flags in `tp_cfg`:

```c
.flags = {
    .swap_xy = 1,   // swap X and Y axes
    .mirror_x = 1,  // flip X
    .mirror_y = 1,  // flip Y
},
```

Set whichever combination matches your panel's physical orientation.

### ❌ `idf.py set-target` fails or is ignored

Make sure you don't have a stale `sdkconfig` from a previous target. Delete it and the build directory, then set the target again:

```bash
rm -rf build sdkconfig
idf.py set-target esp32p4
idf.py build
```

---

## Project Structure

```
gt911_touch/
├── CMakeLists.txt          # Top-level project CMake
├── partitions.csv          # 16 MB flash layout for JC4880P443C
├── sdkconfig.defaults      # Pre-configured for ESP32-P4, PSRAM, 16 MB flash
└── main/
    ├── CMakeLists.txt      # Component registration
    ├── idf_component.yml   # Declares esp_lcd_touch_gt911 dependency
    └── gt911_touch.c       # All the code
```

### sdkconfig.defaults

The project ships a minimal `sdkconfig.defaults` that sets:

- Target: `esp32p4`
- Flash: QIO mode, 16 MB
- PSRAM: enabled
- FreeRTOS tick rate: 1000 Hz
- Partition table: custom (uses `partitions.csv`)

If you're on a different board, these may need adjusting. In particular, if you don't have PSRAM, remove `CONFIG_SPIRAM=y`.

---

## How It Works

1. **Boot** — `test_i2c_probe()` creates a temporary I2C bus and probes address `0x5D` to confirm the GT911 is alive before doing anything else. If this fails, stop here and fix hardware.

2. **Queue** — A FreeRTOS queue (`touch_event_queue`) of 20 × `uint32_t` items is created. The ISR sends into this queue; the processing task reads from it.

3. **Interrupt callback** — `touch_interrupt_callback()` runs in ISR context. It does one thing: puts a notification on the queue and optionally yields to a higher-priority task.

4. **Processing task** — `touch_processing_task()` blocks on `xQueueReceive()`. When it unblocks, it calls `esp_lcd_touch_read_data()` then `esp_lcd_touch_get_data()` and logs all touch points.

5. **Status task** — Optional. Prints total touch count every 10 seconds.

---

## Adapting This to Your Own Project

This is a diagnostics / bringup tool. To integrate touch into a larger project:

- Keep the `touch_init()` function as-is.
- Replace the `touch_processing_task` log statements with your own UI event dispatch.
- Remove `test_i2c_probe()` and `status_report_task()` once you're confident hardware is working.
- The `touch_handle` is global — pass it or wrap it however suits your architecture.

---

## License

MIT — do whatever you want with it.
