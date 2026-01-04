# WiFi Configuration System for Slider Application

## Quick Start Guide

### First Time Setup (AP Provisioning Mode)

When the device has no stored WiFi credentials, it automatically enters AP provisioning mode:

1. **Connect to the WiFi access point** named `PicoW-Setup`
2. **Configure manual IP** on your device (phone/laptop):
   - IP Address: `192.168.4.50`
   - Subnet Mask: `255.255.255.0`
   - Router: `192.168.4.1`
3. **Open web browser** to `http://192.168.4.1`
4. **Select your WiFi network** and enter the password
5. **Submit** - Credentials are saved and device connects to your network

**Note:** Manual IP configuration is required because DHCP server is not supported on the Pico W's CYW43439 WiFi chip in AP mode. This is a one-time setup.

### After Configuration

- Device auto-connects to your WiFi on boot
- HTTP configuration interface available at device's IP address
- IP address displayed in serial console
- Use `wifi reset` command to clear credentials and return to AP mode

## Overview

This application includes a comprehensive WiFi configuration system with the following components:

### Modules Created

1. **wifi_scanner** (`wifi_scanner.c/h`)
   - Scans for available WiFi networks
   - Displays SSID, signal strength, channel, and security type
   - Stores up to 32 scan results
   - Thread-safe with semaphore synchronization

2. **wifi_ap_provisioning** (`wifi_ap_provisioning.c/h`)
   - Framework for creating a WiFi access point for provisioning
   - Note: Limited support on Pico W's CYW43439 in Zephyr
   - Provides callbacks for credential submission

3. **http_server** (`http_server.c/h`)
   - Lightweight HTTP server for web-based configuration
   - Serves HTML interface for network selection
   - Handles credential submission via POST requests

4. **wifi_config_gui** (`wifi_config_gui.c/h`)
   - Display-agnostic GUI framework
   - State machine for user navigation
   - Supports button/input device integration
   - Can be adapted to OLED/LCD displays

5. **wifi_shell_commands** (`wifi_shell_commands.c/h`)
   - Extended shell commands for WiFi management
   - Commands: reset, scan, provision, factory_reset

## Shell Commands

### Basic WiFi Commands
```
wifi set_ssid <ssid>       - Store WiFi SSID
wifi set_password <pass>   - Store WiFi password
wifi connect               - Connect to stored network
wifi status                - Show connection status
```

### Extended Commands
```
wifi_ext reset             - Clear stored credentials
wifi_ext scan              - Scan and display networks
wifi_ext provision         - Start provisioning mode
wifi_ext provision_stop    - Stop provisioning mode
wifi_ext factory_reset     - Clear all settings
```

### Demo Commands
```
demo show                  - Display all settings
kernel reboot              - Reboot device
```

## Usage Flow

### First Boot (No Credentials)
1. Device attempts to enter AP provisioning mode
2. If AP mode fails (likely on Pico W), use shell commands:
   ```
   wifi set_ssid YourNetwork
   wifi set_password YourPassword
   wifi connect
   ```

### Subsequent Boots
1. Device loads credentials from flash
2. Automatically connects to stored network
3. Falls back to provisioning if connection fails

### Manual Reset
```
wifi_ext reset
kernel reboot
```

## Architecture

### Object-Oriented Design
- Each module is a separate "class" with clear interfaces
- Proper encapsulation with private static data
- Well-defined public APIs in header files

### Documentation
- Comprehensive Doxygen-style comments
- Function documentation with parameters and return values
- Module-level documentation in headers

### Error Handling
- Proper errno returns
- Logging at appropriate levels
- Graceful degradation when features unavailable

## Known Limitations

### AP Mode on Pico W
The Raspberry Pi Pico W uses the CYW43439 WiFi chip, which has **limited Access Point mode support** in Zephyr RTOS. The AP provisioning feature is included as a framework but may not function on this hardware.

**Workarounds:**
1. Use shell commands via USB serial for configuration
2. Hard-code credentials during development
3. Consider external WiFi module with AP support
4. Use BLE for provisioning (future enhancement)

### Alternative Provisioning Methods

#### Option 1: Serial Shell (Recommended for Pico W)
Connect via USB serial and use shell commands as documented above.

#### Option 2: Pre-programmed Credentials
Modify `main.c` to set default credentials:
```c
strncpy(wifi_ssid, "YourSSID", WIFI_SSID_MAX);
strncpy(wifi_psk, "YourPassword", WIFI_PSK_MAX);
wifi_credentials_set = true;
settings_save();
```

#### Option 3: BLE Provisioning (Future)
Add Bluetooth Low Energy module for credential transfer from mobile app.

## Configuration Options

### Memory Settings (prj.conf)
```
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_NET_TX_STACK_SIZE=4096
CONFIG_NET_RX_STACK_SIZE=4096
CONFIG_HEAP_MEM_POOL_SIZE=16384
```

### Network Buffers
```
CONFIG_NET_PKT_RX_COUNT=16
CONFIG_NET_PKT_TX_COUNT=16
CONFIG_NET_BUF_RX_COUNT=32
CONFIG_NET_BUF_TX_COUNT=32
```

## Building

Standard Zephyr build process:
```bash
west build -b rpi_pico/rp2040/w
west flash
```

Or with CMake:
```bash
cmake -B build -GNinja
ninja -C build
```

## Troubleshooting

### Build Errors
- Ensure Zephyr SDK is properly installed
- Check that `ZEPHYR_BASE` environment variable is set
- Verify board files exist for `rpi_pico/rp2040/w`

### Runtime Issues
**WiFi Not Scanning:**
- Check that WiFi driver is initialized
- Verify antenna connections on Pico W
- Ensure regulatory domain is set correctly

**Connection Fails:**
- Verify SSID and password are correct
- Check signal strength (`wifi_ext scan`)
- Ensure security type matches (WPA2-PSK typical)

**AP Mode Not Working:**
- This is expected on Pico W - use serial shell instead
- See "Known Limitations" section above

## Future Enhancements

1. **BLE Provisioning** - Use Bluetooth for credential transfer
2. **WPS Support** - Wi-Fi Protected Setup button
3. **QR Code** - Generate QR code for mobile app provisioning
4. **Web UI Improvements** - Enhanced HTML interface
5. **Display Support** - Add OLED/LCD GUI implementation
6. **mDNS/DNS-SD** - Service discovery for easy access
7. **OTA Updates** - Over-the-air firmware updates

## File Structure

```
apps/slider/
├── src/
│   ├── main.c                      - Main application with integration
│   ├── wifi_scanner.c/h            - Network scanning module
│   ├── wifi_ap_provisioning.c/h    - AP mode framework
│   ├── http_server.c/h             - HTTP configuration server
│   ├── wifi_config_gui.c/h         - Display GUI framework
│   └── wifi_shell_commands.c/h     - Extended shell commands
├── boards/
│   └── rpi_pico_rp2040_w.overlay   - Device tree overlay
├── prj.conf                        - Kconfig configuration
└── CMakeLists.txt                  - Build configuration
```

## Code Quality

✅ **Well-Documented** - Comprehensive comments throughout
✅ **Modular** - Separate files for each component
✅ **OOP Design** - Clear class-like structures
✅ **Error Handling** - Proper return codes and logging
✅ **Thread-Safe** - Semaphores and proper synchronization
✅ **Configurable** - Easy to adapt and extend

## License

Same as the parent project (see LICENSE file).

## Support

For issues specific to this WiFi configuration system, check:
1. Zephyr WiFi documentation
2. CYW43439 driver limitations
3. Raspberry Pi Pico W hardware specifications

For general Zephyr questions, consult the official Zephyr documentation.
