/**
 * @file wifi_scanner.h
 * @brief WiFi network scanner module
 *
 * This module provides WiFi network scanning functionality, allowing
 * discovery of available access points with their signal strengths,
 * security types, and other properties.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/wifi_mgmt.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of scan results to store */
#define WIFI_SCANNER_MAX_RESULTS 32

/* Note: We use the wifi_scan_result struct from Zephyr's wifi_mgmt.h
 * instead of defining our own to avoid conflicts. The Zephyr struct
 * already provides all necessary fields. */

/**
 * @brief WiFi scanner state
 */
enum wifi_scanner_state {
	WIFI_SCANNER_IDLE,      /**< Not scanning */
	WIFI_SCANNER_SCANNING,  /**< Scan in progress */
	WIFI_SCANNER_COMPLETE,  /**< Scan completed successfully */
	WIFI_SCANNER_FAILED     /**< Scan failed */
};

/**
 * @brief WiFi scanner context
 *
 * Manages scan state and results
 */
struct wifi_scanner {
	struct wifi_scan_result results[WIFI_SCANNER_MAX_RESULTS];
	size_t result_count;
	enum wifi_scanner_state state;
	struct k_sem scan_sem;
	struct net_mgmt_event_callback scan_cb;
	int scan_status;
};

/**
 * @brief Initialize the WiFi scanner
 *
 * Sets up the scanner context and registers event callbacks
 *
 * @param scanner Pointer to scanner context
 * @return 0 on success, negative errno on failure
 */
int wifi_scanner_init(struct wifi_scanner *scanner);

/**
 * @brief Start a WiFi network scan
 *
 * Initiates a scan for available networks. This is a blocking call
 * that waits for scan completion or timeout.
 *
 * @param scanner Pointer to scanner context
 * @param timeout_ms Timeout in milliseconds (0 = use default 10s)
 * @return 0 on success, negative errno on failure
 */
int wifi_scanner_scan(struct wifi_scanner *scanner, uint32_t timeout_ms);

/**
 * @brief Get scan results
 *
 * @param scanner Pointer to scanner context
 * @param count Output parameter for number of results
 * @return Pointer to results array, or NULL if no results
 */
const struct wifi_scan_result *wifi_scanner_get_results(
	struct wifi_scanner *scanner, size_t *count);

/**
 * @brief Clear scan results
 *
 * @param scanner Pointer to scanner context
 */
void wifi_scanner_clear_results(struct wifi_scanner *scanner);

/**
 * @brief Get scanner state
 *
 * @param scanner Pointer to scanner context
 * @return Current scanner state
 */
enum wifi_scanner_state wifi_scanner_get_state(struct wifi_scanner *scanner);

/**
 * @brief Convert security type to string
 *
 * @param security Security type enum
 * @return Human-readable security type string
 */
const char *wifi_scanner_security_to_string(enum wifi_security_type security);

#ifdef __cplusplus
}
#endif
