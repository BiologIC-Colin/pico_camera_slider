# Slider - Zephyr Settings Demo for Raspberry Pi Pico W

A working demonstration of persistent storage using Zephyr RTOS Settings API with ZMS (Zephyr Memory Storage) backend on Raspberry Pi Pico W.

Part of the pico_camera_slider project - converting a GVM camera slider to use WiFi-based controller.

## Features

- **Persistent Boot Counter**: Automatically increments on each boot and survives power cycles
- **String Storage**: Store and retrieve user strings via shell commands
- **ZMS Backend**: Uses Zephyr Memory Storage for reliable flash storage
- **Shell Interface**: Interactive commands for testing persistence

## Hardware Requirements

- Raspberry Pi Pico W (RP2040 with CYW43439 WiFi)
- USB connection for power and serial console

## Software Requirements

- Zephyr RTOS v4.3.0 or later
- hal_rpi_pico with flash write fix (commit 5a981c7 or later)

## Building

```bash
west build -b rpi_pico/rp2040/w apps/slider
```

## Flash Layout

The application uses a 64KB storage partition at the end of the 2MB flash:

- **Code Partition**: 0x100 - 0x1EFFFF (1.937 MB)
- **Storage Partition**: 0x1F0000 - 0x1FFFFF (64 KB)

Storage partition is configured in `boards/rpi_pico_rp2040_w.overlay`.

## Usage

### Shell Commands

After flashing, connect to the serial console (115200 baud):

```
demo set_string <text>    # Store a string in persistent storage
demo show                  # Display current settings
demo save                  # Manually save all settings to flash
demo load                  # Reload settings from flash
kernel reboot              # Reboot to test persistence
```

### Example Session

```
slider:~$ demo show
Settings:
  Boot count: 5
  User string: <empty>

slider:~$ demo set_string "Hello Zephyr"
String saved: 'Hello Zephyr'

slider:~$ kernel reboot
[...reboot...]
Boot count: 6

slider:~$ demo show
Settings:
  Boot count: 6
  User string: Hello Zephyr
```

## Configuration

### prj.conf

Key configuration options:

```
CONFIG_SETTINGS=y              # Enable Settings API
CONFIG_SETTINGS_ZMS=y          # Use ZMS backend
CONFIG_SETTINGS_RUNTIME=y      # Runtime settings support
CONFIG_ZMS=y                   # Enable ZMS
CONFIG_MPU_ALLOW_FLASH_WRITE=y # Required for RP2040
CONFIG_SHELL=y                 # Shell interface
```

### Devicetree Overlay

The storage partition must not overlap with the code partition. The overlay reduces the code partition size to make room for storage:

```dts
&flash0 {
    partitions {
        code_partition: partition@100 {
            reg = <0x00000100 0x001eff00>;  /* 1.937 MB */
        };
        storage_partition: partition@1f0000 {
            reg = <0x001f0000 0x00010000>;  /* 64 KB */
        };
    };
};
```

## Important Notes

### RP2040 Flash Write Issue

**Critical**: RP2040 has a flash write regression that was fixed in hal_rpi_pico commit 5a981c7c29e3846646549a1902183684f0147e1d. Earlier versions will appear to write successfully but data will not persist. Ensure your hal_rpi_pico is up to date:

```bash
cd ~/zephyrproject
west update hal_rpi_pico
```

### Storage Backend: ZMS vs NVS

This project uses ZMS (Zephyr Memory Storage) instead of NVS (Non-Volatile Storage). ZMS is newer and provides:
- Better wear leveling
- More efficient storage format
- Better compatibility with modern flash controllers

### Settings API Pattern

The Settings API uses three handler callbacks:
- **h_set**: Called when loading values from flash
- **h_commit**: Called after all settings are loaded
- **h_export**: Called by `settings_save()` to enumerate values for writing

All setting keys must include their full path (e.g., `"demo/boot_count"`) when passed to the export callback.

## Troubleshooting

### Settings not persisting

1. Check hal_rpi_pico version: `west list hal_rpi_pico`
2. Verify flash partition doesn't overlap with code partition
3. Check for `CONFIG_MPU_ALLOW_FLASH_WRITE=y` in prj.conf
4. Look for flash driver errors in logs

### Build errors

- Ensure Zephyr is up to date: `west update`
- Clean build directory: `west build -t pristine`

## Next Steps

This base provides a working foundation for:
- WiFi credential storage
- Application configuration persistence
- Device settings management
- Network parameters storage

## License

SPDX-License-Identifier: Apache-2.0
