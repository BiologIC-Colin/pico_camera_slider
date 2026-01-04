cmake_minimum_required(VERSION 3.20.0)

# If you want to force the board from here (optional).
# You can also set BOARD in the CLion CMake profile instead.
set(BOARD rpi_pico/rp2040/w CACHE STRING "Zephyr board")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(slider)

# App sources
target_sources(app PRIVATE
        src/main.c
        src/wifi_scanner.c
        src/wifi_ap_provisioning.c
        src/http_server.c
        src/wifi_config_gui.c
        src/wifi_shell_commands.c
)

# If you keep headers in src/ (e.g., wifi_creds.h), this is optional because
# Zephyr already adds the app dir include path, but it's harmless and explicit.
target_include_directories(app PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
