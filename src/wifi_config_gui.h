/**
 * @file wifi_config_gui.h
 * @brief WiFi configuration GUI module
 *
 * This module provides a display-based GUI for WiFi configuration.
 * It allows users to scan for networks, select an SSID, and enter
 * credentials using buttons or other input devices.
 *
 * Note: This is a framework that can be adapted to various display
 * hardware (OLED, LCD, etc.) and input methods (buttons, touchscreen).
 */

#pragma once

#include <zephyr/kernel.h>
#include "wifi_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GUI state machine states
 */
enum wifi_gui_state {
	WIFI_GUI_IDLE,           /**< GUI not active */
	WIFI_GUI_SCANNING,       /**< Scanning for networks */
	WIFI_GUI_NETWORK_LIST,   /**< Displaying network list */
	WIFI_GUI_ENTER_PASSWORD, /**< Entering password */
	WIFI_GUI_CONNECTING,     /**< Connecting to network */
	WIFI_GUI_SUCCESS,        /**< Successfully connected */
	WIFI_GUI_FAILED          /**< Connection failed */
};

/**
 * @brief GUI input event types
 */
enum wifi_gui_input {
	WIFI_GUI_INPUT_UP,       /**< Navigate up */
	WIFI_GUI_INPUT_DOWN,     /**< Navigate down */
	WIFI_GUI_INPUT_SELECT,   /**< Select/confirm */
	WIFI_GUI_INPUT_BACK,     /**< Go back/cancel */
	WIFI_GUI_INPUT_CHAR      /**< Character input */
};

/**
 * @brief GUI callbacks for display updates
 *
 * These callbacks are called by the GUI module to update the display.
 * The application must implement these functions.
 */
struct wifi_gui_display_ops {
	/**
	 * @brief Clear the display
	 */
	void (*clear)(void);

	/**
	 * @brief Show text message
	 *
	 * @param line Line number (0-based)
	 * @param text Text to display
	 */
	void (*show_text)(int line, const char *text);

	/**
	 * @brief Show network list
	 *
	 * @param results Array of scan results
	 * @param count Number of results
	 * @param selected Index of selected item
	 */
	void (*show_networks)(const struct wifi_scan_result *results,
	                      size_t count, size_t selected);

	/**
	 * @brief Show password entry screen
	 *
	 * @param ssid Network SSID
	 * @param password Current password (may be masked)
	 */
	void (*show_password_entry)(const char *ssid, const char *password);

	/**
	 * @brief Update display (refresh)
	 */
	void (*update)(void);
};

/**
 * @brief Credentials entered callback
 *
 * Called when user completes credential entry
 *
 * @param ssid Selected SSID
 * @param password Entered password
 * @param user_data User data pointer
 */
typedef void (*wifi_gui_creds_cb_t)(const char *ssid,
                                     const char *password,
                                     void *user_data);

/**
 * @brief WiFi configuration GUI context
 */
struct wifi_gui {
	enum wifi_gui_state state;
	struct wifi_scanner *scanner;
	const struct wifi_gui_display_ops *display_ops;
	wifi_gui_creds_cb_t creds_cb;
	void *cb_user_data;

	/* UI state */
	size_t selected_network;
	char selected_ssid[33];
	char entered_password[65];
	size_t password_cursor;
};

/**
 * @brief Initialize the WiFi configuration GUI
 *
 * @param gui Pointer to GUI context
 * @param scanner Pointer to WiFi scanner
 * @param display_ops Display operations callbacks
 * @return 0 on success, negative errno on failure
 */
int wifi_gui_init(struct wifi_gui *gui,
                   struct wifi_scanner *scanner,
                   const struct wifi_gui_display_ops *display_ops);

/**
 * @brief Start the WiFi configuration GUI
 *
 * Begins the configuration process by scanning for networks
 *
 * @param gui Pointer to GUI context
 * @param creds_cb Callback for credential submission
 * @param user_data User data passed to callback
 * @return 0 on success, negative errno on failure
 */
int wifi_gui_start(struct wifi_gui *gui,
                    wifi_gui_creds_cb_t creds_cb,
                    void *user_data);

/**
 * @brief Stop the WiFi configuration GUI
 *
 * @param gui Pointer to GUI context
 * @return 0 on success, negative errno on failure
 */
int wifi_gui_stop(struct wifi_gui *gui);

/**
 * @brief Handle user input event
 *
 * Process button press or other input events
 *
 * @param gui Pointer to GUI context
 * @param input Input event type
 * @param data Optional data (e.g., character for WIFI_GUI_INPUT_CHAR)
 * @return 0 on success, negative errno on failure
 */
int wifi_gui_handle_input(struct wifi_gui *gui,
                           enum wifi_gui_input input,
                           char data);

/**
 * @brief Get current GUI state
 *
 * @param gui Pointer to GUI context
 * @return Current GUI state
 */
enum wifi_gui_state wifi_gui_get_state(struct wifi_gui *gui);

/**
 * @brief Refresh the display
 *
 * Updates the display based on current state
 *
 * @param gui Pointer to GUI context
 */
void wifi_gui_refresh(struct wifi_gui *gui);

#ifdef __cplusplus
}
#endif
