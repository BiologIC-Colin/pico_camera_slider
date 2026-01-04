/**
 * @file http_server.h
 * @brief Simple HTTP server for WiFi provisioning
 *
 * This module provides a lightweight HTTP server that serves a
 * web-based configuration interface for WiFi credential setup.
 */

#pragma once

#include <zephyr/kernel.h>
#include "wifi_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

/** HTTP server port */
#define HTTP_SERVER_PORT 80

/** Maximum number of concurrent connections */
#define HTTP_SERVER_MAX_CONNECTIONS 2

/**
 * @brief HTTP server state
 */
enum http_server_state {
	HTTP_SERVER_STOPPED,    /**< Server not running */
	HTTP_SERVER_STARTING,   /**< Server starting */
	HTTP_SERVER_RUNNING,    /**< Server running */
	HTTP_SERVER_FAILED      /**< Server failed */
};

/**
 * @brief Credentials submission callback
 *
 * Called when user submits WiFi credentials via web interface
 *
 * @param ssid Selected WiFi SSID
 * @param password WiFi password
 * @param user_data User data pointer
 */
typedef void (*http_server_creds_cb_t)(const char *ssid,
                                        const char *password,
                                        void *user_data);

/**
 * @brief HTTP server context
 */
struct http_server {
	enum http_server_state state;
	int listen_sock;
	struct k_thread server_thread;
	k_tid_t server_tid;
	http_server_creds_cb_t creds_cb;
	void *cb_user_data;
	struct wifi_scanner *scanner;  /**< Reference to WiFi scanner */
	bool running;
};

/**
 * @brief Initialize the HTTP server
 *
 * @param server Pointer to HTTP server context
 * @param scanner Pointer to WiFi scanner (for network list)
 * @return 0 on success, negative errno on failure
 */
int http_server_init(struct http_server *server, struct wifi_scanner *scanner);

/**
 * @brief Start the HTTP server
 *
 * Starts listening for HTTP connections and serves the configuration interface
 *
 * @param server Pointer to HTTP server context
 * @param creds_cb Callback for credential submissions
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int http_server_start(struct http_server *server,
                       http_server_creds_cb_t creds_cb,
                       void *user_data);

/**
 * @brief Stop the HTTP server
 *
 * @param server Pointer to HTTP server context
 * @return 0 on success, negative errno on failure
 */
int http_server_stop(struct http_server *server);

/**
 * @brief Get server state
 *
 * @param server Pointer to HTTP server context
 * @return Current server state
 */
enum http_server_state http_server_get_state(struct http_server *server);

#ifdef __cplusplus
}
#endif
