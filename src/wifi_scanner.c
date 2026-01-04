/**
 * @file wifi_scanner.c
 * @brief WiFi network scanner implementation
 */

#include "wifi_scanner.h"
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(wifi_scanner, LOG_LEVEL_INF);

/**
 * @brief WiFi scan result callback
 *
 * Called by the WiFi subsystem for each discovered network
 */
static void wifi_scan_result_handler(struct net_mgmt_event_callback *cb,
                                      uint64_t mgmt_event,
                                      struct net_if *iface)
{
	struct wifi_scanner *scanner = CONTAINER_OF(cb, struct wifi_scanner, scan_cb);
	const struct wifi_scan_result *entry = (const struct wifi_scan_result *)cb->info;

	if (mgmt_event != NET_EVENT_WIFI_SCAN_RESULT) {
		return;
	}

	/* Check if we have space for more results */
	if (scanner->result_count >= WIFI_SCANNER_MAX_RESULTS) {
		LOG_WRN("Scan result buffer full, ignoring result");
		return;
	}

	/* Copy the scan result */
	struct wifi_scan_result *result = &scanner->results[scanner->result_count];
	memcpy(result, entry, sizeof(struct wifi_scan_result));

	/* Ensure SSID is null-terminated */
	if (result->ssid_length < WIFI_SSID_MAX_LEN) {
		result->ssid[result->ssid_length] = '\0';
	} else {
		result->ssid[WIFI_SSID_MAX_LEN] = '\0';
	}

	scanner->result_count++;

	LOG_DBG("Scan result #%zu: SSID=%s, RSSI=%d, Channel=%u, Security=%u",
	        scanner->result_count, result->ssid, result->rssi,
	        result->channel, result->security);
}

/**
 * @brief WiFi scan done callback
 *
 * Called when the scan completes or fails
 */
static void wifi_scan_done_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event,
                                    struct net_if *iface)
{
	struct wifi_scanner *scanner = CONTAINER_OF(cb, struct wifi_scanner, scan_cb);
	const struct wifi_status *status = (const struct wifi_status *)cb->info;

	if (mgmt_event != NET_EVENT_WIFI_SCAN_DONE) {
		return;
	}

	if (status->status == 0) {
		scanner->state = WIFI_SCANNER_COMPLETE;
		scanner->scan_status = 0;
		LOG_INF("WiFi scan completed, found %zu networks", scanner->result_count);
	} else {
		scanner->state = WIFI_SCANNER_FAILED;
		scanner->scan_status = status->status;
		LOG_ERR("WiFi scan failed with status: %d", status->status);
	}

	/* Signal completion */
	k_sem_give(&scanner->scan_sem);
}

int wifi_scanner_init(struct wifi_scanner *scanner)
{
	if (!scanner) {
		return -EINVAL;
	}

	/* Initialize scanner state */
	memset(scanner, 0, sizeof(struct wifi_scanner));
	scanner->state = WIFI_SCANNER_IDLE;
	k_sem_init(&scanner->scan_sem, 0, 1);

	/* Register scan callbacks */
	net_mgmt_init_event_callback(&scanner->scan_cb,
	                             wifi_scan_result_handler,
	                             NET_EVENT_WIFI_SCAN_RESULT);
	net_mgmt_add_event_callback(&scanner->scan_cb);

	/* Note: We reuse the same callback struct but register for done event separately */
	net_mgmt_init_event_callback(&scanner->scan_cb,
	                             wifi_scan_done_handler,
	                             NET_EVENT_WIFI_SCAN_RESULT |
	                             NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&scanner->scan_cb);

	LOG_INF("WiFi scanner initialized");
	return 0;
}

int wifi_scanner_scan(struct wifi_scanner *scanner, uint32_t timeout_ms)
{
	struct net_if *iface;
	int ret;

	if (!scanner) {
		return -EINVAL;
	}

	if (scanner->state == WIFI_SCANNER_SCANNING) {
		LOG_WRN("Scan already in progress");
		return -EBUSY;
	}

	/* Get network interface */
	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	/* Clear previous results */
	wifi_scanner_clear_results(scanner);

	/* Reset semaphore */
	k_sem_reset(&scanner->scan_sem);

	/* Update state */
	scanner->state = WIFI_SCANNER_SCANNING;
	scanner->scan_status = 0;

	LOG_INF("Starting WiFi scan...");

	/* Trigger scan */
	ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
	if (ret) {
		LOG_ERR("Failed to start WiFi scan: %d", ret);
		scanner->state = WIFI_SCANNER_FAILED;
		scanner->scan_status = ret;
		return ret;
	}

	/* Wait for scan completion */
	if (timeout_ms == 0) {
		timeout_ms = 10000; /* Default 10 second timeout */
	}

	ret = k_sem_take(&scanner->scan_sem, K_MSEC(timeout_ms));
	if (ret == -EAGAIN) {
		LOG_ERR("WiFi scan timeout");
		scanner->state = WIFI_SCANNER_FAILED;
		scanner->scan_status = -ETIMEDOUT;
		return -ETIMEDOUT;
	}

	/* Return scan status */
	if (scanner->scan_status != 0) {
		return scanner->scan_status;
	}

	return 0;
}

const struct wifi_scan_result *wifi_scanner_get_results(
	struct wifi_scanner *scanner, size_t *count)
{
	if (!scanner || !count) {
		return NULL;
	}

	*count = scanner->result_count;
	return scanner->results;
}

void wifi_scanner_clear_results(struct wifi_scanner *scanner)
{
	if (!scanner) {
		return;
	}

	memset(scanner->results, 0, sizeof(scanner->results));
	scanner->result_count = 0;
}

enum wifi_scanner_state wifi_scanner_get_state(struct wifi_scanner *scanner)
{
	if (!scanner) {
		return WIFI_SCANNER_IDLE;
	}

	return scanner->state;
}

const char *wifi_scanner_security_to_string(enum wifi_security_type security)
{
	switch (security) {
	case WIFI_SECURITY_TYPE_NONE:
		return "Open";
	case WIFI_SECURITY_TYPE_PSK:
		return "WPA2-PSK";
	case WIFI_SECURITY_TYPE_PSK_SHA256:
		return "WPA2-PSK-SHA256";
	case WIFI_SECURITY_TYPE_SAE:
		return "WPA3-SAE";
	case WIFI_SECURITY_TYPE_WAPI:
		return "WAPI";
	case WIFI_SECURITY_TYPE_EAP:
		return "WPA2-EAP";
	default:
		return "Unknown";
	}
}
