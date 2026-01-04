/**
 * @file wifi_ap_provisioning.h
 * @brief WiFi Access Point provisioning module
 *
 * This module creates a SoftAP (access point) that allows users to
 * configure WiFi credentials via a web interface when no stored
 * credentials are available.
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/net/net_mgmt.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default AP SSID */
#define WIFI_AP_DEFAULT_SSID "PicoW-Setup"

/** Default AP password (empty = open network) */
#define WIFI_AP_DEFAULT_PASSWORD ""

/** Default AP channel */
#define WIFI_AP_DEFAULT_CHANNEL 6

/** Default AP IP address */
#define WIFI_AP_DEFAULT_IP "192.168.4.1"

/**
 * @brief WiFi AP provisioning state
 */
enum wifi_ap_state {
	WIFI_AP_IDLE,           /**< AP not started */
	WIFI_AP_STARTING,       /**< AP starting */
	WIFI_AP_ACTIVE,         /**< AP active and ready */
	WIFI_AP_FAILED          /**< AP failed to start */
};

/**
 * @brief WiFi AP configuration
 */
struct wifi_ap_config {
	char ssid[33];          /**< AP SSID */
	char password[65];      /**< AP password (empty for open) */
	uint8_t channel;        /**< WiFi channel */
	char ip_addr[16];       /**< AP IP address */
};

/**
 * @brief WiFi AP provisioning context
 */
struct wifi_ap_provisioning {
	struct wifi_ap_config config;
	enum wifi_ap_state state;
	struct net_mgmt_event_callback ap_cb;
	bool credentials_received;
	char new_ssid[33];      /**< New credentials from user */
	char new_password[65];  /**< New password from user */
};

/**
 * @brief Credentials received callback type
 *
 * Called when new WiFi credentials are received via the provisioning interface
 *
 * @param ssid New WiFi SSID
 * @param password New WiFi password
 * @param user_data User data pointer
 */
typedef void (*wifi_ap_creds_received_cb_t)(const char *ssid,
                                             const char *password,
                                             void *user_data);

/**
 * @brief Initialize the WiFi AP provisioning module
 *
 * @param ap Pointer to AP provisioning context
 * @param config AP configuration (NULL = use defaults)
 * @return 0 on success, negative errno on failure
 */
int wifi_ap_provisioning_init(struct wifi_ap_provisioning *ap,
                               const struct wifi_ap_config *config);

/**
 * @brief Start the WiFi access point
 *
 * Creates a SoftAP and starts the HTTP configuration server
 *
 * @param ap Pointer to AP provisioning context
 * @param creds_cb Callback for when credentials are received
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int wifi_ap_provisioning_start(struct wifi_ap_provisioning *ap,
                                wifi_ap_creds_received_cb_t creds_cb,
                                void *user_data);

/**
 * @brief Stop the WiFi access point
 *
 * Stops the SoftAP and HTTP server
 *
 * @param ap Pointer to AP provisioning context
 * @return 0 on success, negative errno on failure
 */
int wifi_ap_provisioning_stop(struct wifi_ap_provisioning *ap);

/**
 * @brief Get AP provisioning state
 *
 * @param ap Pointer to AP provisioning context
 * @return Current AP state
 */
enum wifi_ap_state wifi_ap_provisioning_get_state(struct wifi_ap_provisioning *ap);

/**
 * @brief Check if credentials have been received
 *
 * @param ap Pointer to AP provisioning context
 * @return true if credentials received, false otherwise
 */
bool wifi_ap_provisioning_has_credentials(struct wifi_ap_provisioning *ap);

#ifdef __cplusplus
}
#endif
