/**
 * @file wifi_config_gui.c
 * @brief WiFi configuration GUI implementation
 */

#include "wifi_config_gui.h"
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(wifi_gui, LOG_LEVEL_INF);

int wifi_gui_init(struct wifi_gui *gui,
                   struct wifi_scanner *scanner,
                   const struct wifi_gui_display_ops *display_ops)
{
	if (!gui || !scanner || !display_ops) {
		return -EINVAL;
	}

	memset(gui, 0, sizeof(struct wifi_gui));
	gui->state = WIFI_GUI_IDLE;
	gui->scanner = scanner;
	gui->display_ops = display_ops;

	LOG_INF("WiFi GUI initialized");
	return 0;
}

int wifi_gui_start(struct wifi_gui *gui,
                    wifi_gui_creds_cb_t creds_cb,
                    void *user_data)
{
	int ret;

	if (!gui) {
		return -EINVAL;
	}

	gui->creds_cb = creds_cb;
	gui->cb_user_data = user_data;
	gui->selected_network = 0;
	gui->password_cursor = 0;
	gui->entered_password[0] = '\0';

	/* Clear display */
	if (gui->display_ops->clear) {
		gui->display_ops->clear();
	}

	/* Show scanning message */
	if (gui->display_ops->show_text) {
		gui->display_ops->show_text(0, "WiFi Setup");
		gui->display_ops->show_text(1, "Scanning...");
	}

	if (gui->display_ops->update) {
		gui->display_ops->update();
	}

	gui->state = WIFI_GUI_SCANNING;
	LOG_INF("Starting WiFi scan...");

	/* Perform scan */
	ret = wifi_scanner_scan(gui->scanner, 10000);
	if (ret) {
		LOG_ERR("WiFi scan failed: %d", ret);
		gui->state = WIFI_GUI_FAILED;
		if (gui->display_ops->show_text) {
			gui->display_ops->show_text(1, "Scan failed!");
		}
		return ret;
	}

	/* Show network list */
	gui->state = WIFI_GUI_NETWORK_LIST;
	wifi_gui_refresh(gui);

	return 0;
}

int wifi_gui_stop(struct wifi_gui *gui)
{
	if (!gui) {
		return -EINVAL;
	}

	gui->state = WIFI_GUI_IDLE;

	if (gui->display_ops->clear) {
		gui->display_ops->clear();
	}

	if (gui->display_ops->update) {
		gui->display_ops->update();
	}

	LOG_INF("WiFi GUI stopped");
	return 0;
}

int wifi_gui_handle_input(struct wifi_gui *gui,
                           enum wifi_gui_input input,
                           char data)
{
	size_t count;
	const struct wifi_scan_result *results;

	if (!gui) {
		return -EINVAL;
	}

	switch (gui->state) {
	case WIFI_GUI_NETWORK_LIST:
		results = wifi_scanner_get_results(gui->scanner, &count);

		if (input == WIFI_GUI_INPUT_UP) {
			if (gui->selected_network > 0) {
				gui->selected_network--;
				wifi_gui_refresh(gui);
			}
		} else if (input == WIFI_GUI_INPUT_DOWN) {
			if (gui->selected_network < count - 1) {
				gui->selected_network++;
				wifi_gui_refresh(gui);
			}
		} else if (input == WIFI_GUI_INPUT_SELECT) {
			/* Network selected, check if password needed */
			if (results && gui->selected_network < count) {
				strncpy(gui->selected_ssid,
				        results[gui->selected_network].ssid,
				        sizeof(gui->selected_ssid) - 1);
				gui->selected_ssid[sizeof(gui->selected_ssid) - 1] = '\0';

				if (results[gui->selected_network].security == WIFI_SECURITY_TYPE_NONE) {
					/* Open network, connect immediately */
					gui->entered_password[0] = '\0';
					if (gui->creds_cb) {
						gui->creds_cb(gui->selected_ssid,
						              gui->entered_password,
						              gui->cb_user_data);
					}
					gui->state = WIFI_GUI_CONNECTING;
				} else {
					/* Secured network, need password */
					gui->state = WIFI_GUI_ENTER_PASSWORD;
					gui->entered_password[0] = '\0';
					gui->password_cursor = 0;
				}
				wifi_gui_refresh(gui);
			}
		}
		break;

	case WIFI_GUI_ENTER_PASSWORD:
		if (input == WIFI_GUI_INPUT_CHAR && data) {
			/* Add character to password */
			size_t len = strlen(gui->entered_password);
			if (len < sizeof(gui->entered_password) - 1) {
				gui->entered_password[len] = data;
				gui->entered_password[len + 1] = '\0';
				wifi_gui_refresh(gui);
			}
		} else if (input == WIFI_GUI_INPUT_BACK) {
			/* Remove last character or go back */
			size_t len = strlen(gui->entered_password);
			if (len > 0) {
				gui->entered_password[len - 1] = '\0';
				wifi_gui_refresh(gui);
			} else {
				/* Go back to network list */
				gui->state = WIFI_GUI_NETWORK_LIST;
				wifi_gui_refresh(gui);
			}
		} else if (input == WIFI_GUI_INPUT_SELECT) {
			/* Submit credentials */
			if (gui->creds_cb) {
				gui->creds_cb(gui->selected_ssid,
				              gui->entered_password,
				              gui->cb_user_data);
			}
			gui->state = WIFI_GUI_CONNECTING;
			wifi_gui_refresh(gui);
		}
		break;

	default:
		break;
	}

	return 0;
}

enum wifi_gui_state wifi_gui_get_state(struct wifi_gui *gui)
{
	if (!gui) {
		return WIFI_GUI_IDLE;
	}

	return gui->state;
}

void wifi_gui_refresh(struct wifi_gui *gui)
{
	size_t count;
	const struct wifi_scan_result *results;

	if (!gui || !gui->display_ops) {
		return;
	}

	if (gui->display_ops->clear) {
		gui->display_ops->clear();
	}

	switch (gui->state) {
	case WIFI_GUI_SCANNING:
		if (gui->display_ops->show_text) {
			gui->display_ops->show_text(0, "WiFi Setup");
			gui->display_ops->show_text(1, "Scanning...");
		}
		break;

	case WIFI_GUI_NETWORK_LIST:
		results = wifi_scanner_get_results(gui->scanner, &count);
		if (gui->display_ops->show_networks && results && count > 0) {
			gui->display_ops->show_networks(results, count, gui->selected_network);
		} else if (gui->display_ops->show_text) {
			gui->display_ops->show_text(0, "No networks found");
			gui->display_ops->show_text(1, "Press BACK to rescan");
		}
		break;

	case WIFI_GUI_ENTER_PASSWORD:
		if (gui->display_ops->show_password_entry) {
			gui->display_ops->show_password_entry(gui->selected_ssid,
			                                      gui->entered_password);
		} else if (gui->display_ops->show_text) {
			char line[64];
			snprintf(line, sizeof(line), "SSID: %s", gui->selected_ssid);
			gui->display_ops->show_text(0, line);
			snprintf(line, sizeof(line), "Password: %s_", gui->entered_password);
			gui->display_ops->show_text(1, line);
		}
		break;

	case WIFI_GUI_CONNECTING:
		if (gui->display_ops->show_text) {
			gui->display_ops->show_text(0, "Connecting...");
			gui->display_ops->show_text(1, gui->selected_ssid);
		}
		break;

	case WIFI_GUI_SUCCESS:
		if (gui->display_ops->show_text) {
			gui->display_ops->show_text(0, "Connected!");
			gui->display_ops->show_text(1, gui->selected_ssid);
		}
		break;

	case WIFI_GUI_FAILED:
		if (gui->display_ops->show_text) {
			gui->display_ops->show_text(0, "Connection failed");
			gui->display_ops->show_text(1, "Press BACK to retry");
		}
		break;

	default:
		break;
	}

	if (gui->display_ops->update) {
		gui->display_ops->update();
	}
}
