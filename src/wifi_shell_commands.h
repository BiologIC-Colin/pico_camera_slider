/**
 * @file wifi_shell_commands.h
 * @brief Extended WiFi shell commands
 *
 * This module provides additional shell commands for WiFi management,
 * including credential reset, network scanning, and provisioning control.
 */

#pragma once

#include <zephyr/kernel.h>
#include "wifi_scanner.h"
#include "wifi_ap_provisioning.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi shell commands
 *
 * Registers extended WiFi commands with the shell subsystem
 *
 * @param scanner Pointer to WiFi scanner context (optional, can be NULL)
 * @param ap_prov Pointer to AP provisioning context (optional, can be NULL)
 * @return 0 on success, negative errno on failure
 */
int wifi_shell_commands_init(struct wifi_scanner *scanner,
                              struct wifi_ap_provisioning *ap_prov);

#ifdef __cplusplus
}
#endif
