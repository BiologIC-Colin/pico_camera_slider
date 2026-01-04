/**
 * @file wifi_ap_provisioning.c
 * @brief WiFi Access Point provisioning implementation
 */

#include "wifi_ap_provisioning.h"
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/dhcpv4_server.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <arpa/inet.h>

LOG_MODULE_REGISTER(wifi_ap, LOG_LEVEL_INF);

/* Static instance for callback access */
static struct wifi_ap_provisioning *g_ap_instance = NULL;
static wifi_ap_creds_received_cb_t g_creds_callback = NULL;
static void *g_callback_user_data = NULL;

/**
 * @brief WiFi AP event handler
 *
 * Handles AP enable/disable events
 */
static void wifi_ap_event_handler(struct net_mgmt_event_callback *cb,
                                   uint64_t mgmt_event,
                                   struct net_if *iface)
{
	struct wifi_ap_provisioning *ap = g_ap_instance;

	if (!ap) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
	{
		const struct wifi_status *status = (const struct wifi_status *)cb->info;
		if (status && status->status == 0) {
			ap->state = WIFI_AP_ACTIVE;
			LOG_INF("WiFi AP enabled successfully");
		} else {
			ap->state = WIFI_AP_FAILED;
			LOG_ERR("WiFi AP enable failed: %d", status ? status->status : -1);
		}
		break;
	}
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
	{
		ap->state = WIFI_AP_IDLE;
		LOG_INF("WiFi AP disabled");
		break;
	}
	default:
		break;
	}
}

int wifi_ap_provisioning_init(struct wifi_ap_provisioning *ap,
                               const struct wifi_ap_config *config)
{
	if (!ap) {
		return -EINVAL;
	}

	/* Initialize context */
	memset(ap, 0, sizeof(struct wifi_ap_provisioning));
	ap->state = WIFI_AP_IDLE;

	/* Set configuration (use defaults if not provided) */
	if (config) {
		memcpy(&ap->config, config, sizeof(struct wifi_ap_config));
	} else {
		/* Use default configuration */
		strncpy(ap->config.ssid, WIFI_AP_DEFAULT_SSID, sizeof(ap->config.ssid) - 1);
		strncpy(ap->config.password, WIFI_AP_DEFAULT_PASSWORD, sizeof(ap->config.password) - 1);
		ap->config.channel = WIFI_AP_DEFAULT_CHANNEL;
		strncpy(ap->config.ip_addr, WIFI_AP_DEFAULT_IP, sizeof(ap->config.ip_addr) - 1);
	}

	/* Store global instance for callbacks */
	g_ap_instance = ap;

	/* Register AP event callbacks */
	net_mgmt_init_event_callback(&ap->ap_cb,
	                             wifi_ap_event_handler,
	                             NET_EVENT_WIFI_AP_ENABLE_RESULT |
	                             NET_EVENT_WIFI_AP_DISABLE_RESULT);
	net_mgmt_add_event_callback(&ap->ap_cb);

	LOG_INF("WiFi AP provisioning initialized (SSID: %s)", ap->config.ssid);
	return 0;
}

int wifi_ap_provisioning_start(struct wifi_ap_provisioning *ap,
                                wifi_ap_creds_received_cb_t creds_cb,
                                void *user_data)
{
	struct net_if *iface;
	struct wifi_connect_req_params ap_params = {0};
	int ret;

	if (!ap) {
		return -EINVAL;
	}

	if (ap->state == WIFI_AP_ACTIVE || ap->state == WIFI_AP_STARTING) {
		LOG_WRN("AP already active or starting");
		return -EALREADY;
	}

	/* Store callback */
	g_creds_callback = creds_cb;
	g_callback_user_data = user_data;

	/* Get network interface */
	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	/* Configure AP parameters */
	ap_params.ssid = ap->config.ssid;
	ap_params.ssid_length = strlen(ap->config.ssid);
	ap_params.channel = ap->config.channel;
	ap_params.band = WIFI_FREQ_BAND_2_4_GHZ;

	/* Set security based on password */
	if (strlen(ap->config.password) > 0) {
		ap_params.security = WIFI_SECURITY_TYPE_PSK;
		ap_params.psk = ap->config.password;
		ap_params.psk_length = strlen(ap->config.password);
	} else {
		ap_params.security = WIFI_SECURITY_TYPE_NONE;
	}

	ap->state = WIFI_AP_STARTING;

	LOG_INF("Starting WiFi AP (SSID: %s, Channel: %u, Security: %s)",
	        ap_params.ssid, ap_params.channel,
	        ap_params.security == WIFI_SECURITY_TYPE_NONE ? "Open" : "WPA2-PSK");

	/* Enable AP mode */
	ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface, &ap_params, sizeof(ap_params));
	if (ret) {
		LOG_ERR("Failed to start WiFi AP: %d", ret);
		ap->state = WIFI_AP_FAILED;
		return ret;
	}

	/* Configure static IP address for AP interface */
	struct in_addr addr, netmask;
	struct net_if_addr *ifaddr;

	/* Set IP address: 192.168.4.1 */
	if (net_addr_pton(AF_INET, WIFI_AP_DEFAULT_IP, &addr) < 0) {
		LOG_ERR("Failed to parse AP IP address");
	} else {
		ifaddr = net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0);
		if (ifaddr) {
			LOG_INF("AP IP address set to %s", WIFI_AP_DEFAULT_IP);
		} else {
			LOG_ERR("Failed to add AP IP address");
		}
	}

	/* Set netmask: 255.255.255.0 */
	if (net_addr_pton(AF_INET, "255.255.255.0", &netmask) < 0) {
		LOG_ERR("Failed to parse netmask");
	} else {
		net_if_ipv4_set_netmask_by_addr(iface, &addr, &netmask);
	}

	/* Start DHCP server for clients */
	struct in_addr pool_start;
	printk("Attempting to start DHCP server...\n");

	/* Manually set the pool start address: 192.168.4.10 */
	pool_start.s_addr = htonl(0xC0A80A0A); /* 192.168.4.10 in network byte order */

	ret = net_dhcpv4_server_start(iface, &pool_start);
	if (ret == 0) {
		LOG_INF("DHCP server started (pool: 192.168.4.10-192.168.4.254)");
		printk("DHCP server started successfully (pool: 192.168.4.10-254)\n");
	} else {
		LOG_ERR("Failed to start DHCP server: error %d", ret);
		printk("ERROR: DHCP server failed to start: %d\n", ret);
		printk("Clients will need manual IP configuration (192.168.4.x/24)\n");
	}

	/* Note: The actual state will be updated by the event handler */

	return 0;
}

int wifi_ap_provisioning_stop(struct wifi_ap_provisioning *ap)
{
	struct net_if *iface;
	int ret;

	if (!ap) {
		return -EINVAL;
	}

	if (ap->state != WIFI_AP_ACTIVE) {
		LOG_WRN("AP not active");
		return -EALREADY;
	}

	/* Get network interface */
	iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	LOG_INF("Stopping WiFi AP");

	/* Disable AP mode */
	ret = net_mgmt(NET_REQUEST_WIFI_AP_DISABLE, iface, NULL, 0);
	if (ret) {
		LOG_ERR("Failed to stop WiFi AP: %d", ret);
		return ret;
	}

	/* TODO: Stop HTTP server */

	return 0;
}

enum wifi_ap_state wifi_ap_provisioning_get_state(struct wifi_ap_provisioning *ap)
{
	if (!ap) {
		return WIFI_AP_IDLE;
	}

	return ap->state;
}

bool wifi_ap_provisioning_has_credentials(struct wifi_ap_provisioning *ap)
{
	if (!ap) {
		return false;
	}

	return ap->credentials_received;
}

/**
 * @brief Process received credentials (called by HTTP server)
 *
 * This function would be called by the HTTP server when credentials
 * are submitted via the web interface.
 *
 * @param ssid New WiFi SSID
 * @param password New WiFi password
 */
void wifi_ap_provisioning_set_credentials(const char *ssid, const char *password)
{
	struct wifi_ap_provisioning *ap = g_ap_instance;

	if (!ap || !ssid) {
		return;
	}

	/* Store credentials */
	strncpy(ap->new_ssid, ssid, sizeof(ap->new_ssid) - 1);
	ap->new_ssid[sizeof(ap->new_ssid) - 1] = '\0';

	if (password) {
		strncpy(ap->new_password, password, sizeof(ap->new_password) - 1);
		ap->new_password[sizeof(ap->new_password) - 1] = '\0';
	} else {
		ap->new_password[0] = '\0';
	}

	ap->credentials_received = true;

	LOG_INF("Credentials received: SSID=%s", ap->new_ssid);

	/* Call user callback if registered */
	if (g_creds_callback) {
		g_creds_callback(ap->new_ssid, ap->new_password, g_callback_user_data);
	}
}
