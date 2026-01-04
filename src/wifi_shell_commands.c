/**
 * @file wifi_shell_commands.c
 * @brief Extended WiFi shell commands implementation
 */

#include "wifi_shell_commands.h"
#include <zephyr/shell/shell.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(wifi_shell, LOG_LEVEL_INF);

/* Module-level references to scanner and AP provisioning */
static struct wifi_scanner *g_scanner = NULL;
static struct wifi_ap_provisioning *g_ap_prov = NULL;

/**
 * @brief Shell command: Reset WiFi credentials
 *
 * Note: This is deprecated. Use 'wifi reset' instead.
 * This command is kept for backward compatibility but redirects to the main reset.
 */
static int cmd_wifi_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Note: Please use 'wifi reset' command instead");
	shell_print(sh, "This provides proper credential clearing");
	return 0;
}

/**
 * @brief Shell command: Scan for WiFi networks
 *
 * Performs a WiFi scan and displays results
 */
static int cmd_wifi_scan(const struct shell *sh, size_t argc, char **argv)
{
	int rc;
	size_t count;
	const struct wifi_scan_result *results;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_scanner) {
		shell_error(sh, "WiFi scanner not initialized");
		return -ENOTSUP;
	}

	shell_print(sh, "Scanning for WiFi networks...");

	rc = wifi_scanner_scan(g_scanner, 10000);
	if (rc) {
		shell_error(sh, "Scan failed: %d", rc);
		return rc;
	}

	results = wifi_scanner_get_results(g_scanner, &count);
	if (!results || count == 0) {
		shell_print(sh, "No networks found");
		return 0;
	}

	shell_print(sh, "\nFound %zu networks:\n", count);
	shell_print(sh, "%-32s %6s %4s %s", "SSID", "Signal", "Ch", "Security");
	shell_print(sh, "%-32s %6s %4s %s", "----", "------", "--", "--------");

	for (size_t i = 0; i < count; i++) {
		shell_print(sh, "%-32s %4d dBm %2u  %s",
		            results[i].ssid,
		            results[i].rssi,
		            results[i].channel,
		            wifi_scanner_security_to_string(results[i].security));
	}

	shell_print(sh, "");
	return 0;
}

/**
 * @brief Shell command: Start AP provisioning mode
 *
 * Manually starts the access point for WiFi configuration
 */
static int cmd_wifi_provision(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_ap_prov) {
		shell_error(sh, "AP provisioning not initialized");
		return -ENOTSUP;
	}

	shell_print(sh, "Starting provisioning access point...");

	rc = wifi_ap_provisioning_start(g_ap_prov, NULL, NULL);
	if (rc) {
		shell_error(sh, "Failed to start AP: %d", rc);
		return rc;
	}

	shell_print(sh, "Provisioning AP started");
	shell_print(sh, "Connect to SSID: %s", WIFI_AP_DEFAULT_SSID);
	shell_print(sh, "Open browser to: http://%s", WIFI_AP_DEFAULT_IP);

	return 0;
}

/**
 * @brief Shell command: Stop AP provisioning mode
 *
 * Stops the access point
 */
static int cmd_wifi_provision_stop(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_ap_prov) {
		shell_error(sh, "AP provisioning not initialized");
		return -ENOTSUP;
	}

	shell_print(sh, "Stopping provisioning access point...");

	rc = wifi_ap_provisioning_stop(g_ap_prov);
	if (rc) {
		shell_error(sh, "Failed to stop AP: %d", rc);
		return rc;
	}

	shell_print(sh, "Provisioning AP stopped");
	return 0;
}

/**
 * @brief Shell command: Clear all settings and reboot
 *
 * Factory reset - clears all stored settings
 */
static int cmd_wifi_factory_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "WARNING: This will erase all stored settings!");
	shell_print(sh, "Use 'wifi reset' to only clear WiFi credentials");
	shell_print(sh, "");
	shell_print(sh, "To proceed, run: settings delete demo");

	return 0;
}

/* Define subcommands */
SHELL_STATIC_SUBCMD_SET_CREATE(wifi_ext_cmds,
	SHELL_CMD(reset, NULL,
	          "Clear stored WiFi credentials",
	          cmd_wifi_reset),
	SHELL_CMD(scan, NULL,
	          "Scan for available WiFi networks",
	          cmd_wifi_scan),
	SHELL_CMD(provision, NULL,
	          "Start AP provisioning mode",
	          cmd_wifi_provision),
	SHELL_CMD(provision_stop, NULL,
	          "Stop AP provisioning mode",
	          cmd_wifi_provision_stop),
	SHELL_CMD(factory_reset, NULL,
	          "Factory reset (clear all settings)",
	          cmd_wifi_factory_reset),
	SHELL_SUBCMD_SET_END
);

/* Register parent command */
SHELL_CMD_REGISTER(wifi_ext, &wifi_ext_cmds,
                   "Extended WiFi management commands", NULL);

int wifi_shell_commands_init(struct wifi_scanner *scanner,
                              struct wifi_ap_provisioning *ap_prov)
{
	g_scanner = scanner;
	g_ap_prov = ap_prov;

	LOG_INF("WiFi shell commands initialized");
	return 0;
}
