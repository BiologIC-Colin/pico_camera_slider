/*
 * WiFi Configuration System for Raspberry Pi Pico W
 *
 * Demonstrates comprehensive WiFi management using Zephyr Settings API with ZMS backend.
 *
 * Features:
 * - Boot counter that increments on each reboot
 * - WiFi credential storage (SSID and password)
 * - Automatic WiFi connection on boot if credentials are stored
 * - WiFi network scanning
 * - AP provisioning mode with HTTP configuration interface
 * - GUI framework for display-based configuration
 * - Extended shell commands for management
 * - Persistent storage survives power cycles
 *
 * Shell Commands:
 *   wifi set_ssid <ssid>      - Store WiFi SSID
 *   wifi set_password <pass>  - Store WiFi password
 *   wifi connect              - Connect to WiFi using stored credentials
 *   wifi status               - Show WiFi connection status
 *   wifi_ext reset            - Clear stored WiFi credentials
 *   wifi_ext scan             - Scan for available networks
 *   wifi_ext provision        - Start AP provisioning mode
 *   demo show                 - Display current settings
 *   kernel reboot             - Reboot to test persistence
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_ip.h>
#include <string.h>

/* WiFi configuration modules */
#include "wifi_scanner.h"
#include "wifi_ap_provisioning.h"
#include "http_server.h"
#include "wifi_config_gui.h"
#include "wifi_shell_commands.h"

#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(storage_partition)

/* Settings values */
static uint32_t boot_count = 0;

/* WiFi credentials */
#define WIFI_SSID_MAX 32
#define WIFI_PSK_MAX 64
static char wifi_ssid[WIFI_SSID_MAX + 1] = "";
static char wifi_psk[WIFI_PSK_MAX + 1] = "";
static bool wifi_credentials_set = false;

/* WiFi connection state */
static struct net_mgmt_event_callback wifi_cb;
static bool wifi_connected = false;
static K_SEM_DEFINE(wifi_connected_sem, 0, 1);

/* WiFi configuration system components */
static struct wifi_scanner scanner;
static struct wifi_ap_provisioning ap_prov;
static struct http_server http_srv;
static bool provisioning_mode = false;

/* Forward declarations */
static void provisioning_creds_received(const char *ssid,
                                         const char *password,
                                         void *user_data);
static void start_http_server(void);

/*
 * Settings handler: Set (called when loading from storage)
 */
static int demo_handle_set(const char *name, size_t len,
                           settings_read_cb read_cb, void *cb_arg)
{
    int rc;

    if (strcmp(name, "boot_count") == 0) {
        if (len != sizeof(boot_count)) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, &boot_count, sizeof(boot_count));
        if (rc >= 0) {
            printk("Loaded boot_count = %u\n", boot_count);
            return 0;
        }
        return rc;
    }

    if (strcmp(name, "wifi_ssid") == 0) {
        if (len > WIFI_SSID_MAX) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, wifi_ssid, len);
        if (rc >= 0) {
            wifi_ssid[len] = '\0';
            printk("Loaded WiFi SSID = '%s'\n", wifi_ssid);
            return 0;
        }
        return rc;
    }

    if (strcmp(name, "wifi_psk") == 0) {
        if (len > WIFI_PSK_MAX) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, wifi_psk, len);
        if (rc >= 0) {
            wifi_psk[len] = '\0';
            printk("Loaded WiFi password (***)\n");
            wifi_credentials_set = true;
            return 0;
        }
        return rc;
    }

    return -ENOENT;
}

/*
 * Settings handler: Commit (called after all settings loaded)
 */
static int demo_handle_commit(void)
{
    printk("Settings loaded successfully\n");
    return 0;
}

/*
 * Settings handler: Export (called by settings_save() to enumerate values)
 */
static int demo_handle_export(int (*cb)(const char *name,
                                        const void *value,
                                        size_t val_len))
{
    /* Export with full path including subtree prefix */
    (void)cb("demo/boot_count", &boot_count, sizeof(boot_count));

    if (strlen(wifi_ssid) > 0) {
        (void)cb("demo/wifi_ssid", wifi_ssid, strlen(wifi_ssid));
    }

    if (strlen(wifi_psk) > 0) {
        (void)cb("demo/wifi_psk", wifi_psk, strlen(wifi_psk));
    }

    return 0;
}

/* Register static handler for "demo" subtree */
SETTINGS_STATIC_HANDLER_DEFINE(demo_handler, "demo",
                               NULL,                 /* h_get */
                               demo_handle_set,      /* h_set */
                               demo_handle_commit,   /* h_commit */
                               demo_handle_export);  /* h_export */

/*
 * WiFi event handler
 */
static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint64_t mgmt_event, struct net_if *iface)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    switch (mgmt_event) {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        if (status->status == 0) {
            wifi_connected = true;
            printk("Connected\n");
        } else {
            printk("Connection failed (status: %d)\n", status->status);
        }
        k_sem_give(&wifi_connected_sem);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        wifi_connected = false;
        printk("Disconnected\n");
        break;
    default:
        break;
    }
}

/*
 * Connect to WiFi using stored credentials
 */
static int wifi_connect_stored(void)
{
    struct net_if *iface = net_if_get_default();
    struct wifi_connect_req_params params = {0};

    if (!iface) {
        printk("ERROR: No network interface found\n");
        return -ENODEV;
    }

    if (strlen(wifi_ssid) == 0) {
        printk("ERROR: No WiFi SSID configured\n");
        return -EINVAL;
    }

    printk("Connecting to WiFi SSID: %s\n", wifi_ssid);

    params.ssid = wifi_ssid;
    params.ssid_length = strlen(wifi_ssid);
    params.psk = wifi_psk;
    params.psk_length = strlen(wifi_psk);
    params.channel = WIFI_CHANNEL_ANY;
    params.security = (strlen(wifi_psk) > 0) ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;
    params.band = WIFI_FREQ_BAND_2_4_GHZ;
    params.mfp = WIFI_MFP_OPTIONAL;

    /* Reset semaphore before connecting */
    k_sem_reset(&wifi_connected_sem);

    /* Send connection request - this is asynchronous */
    net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));

    /* Wait for connection result event */
    if (k_sem_take(&wifi_connected_sem, K_SECONDS(30)) != 0) {
        printk("Connection timeout\n");
        return -ETIMEDOUT;
    }

    return wifi_connected ? 0 : -ENOEXEC;
}

/*
 * Shell command: set WiFi SSID
 */
static int cmd_wifi_set_ssid(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (argc != 2) {
        shell_error(sh, "Usage: set_ssid <ssid>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > WIFI_SSID_MAX) {
        shell_error(sh, "SSID too long (max %d)", WIFI_SSID_MAX);
        return -EINVAL;
    }

    strncpy(wifi_ssid, argv[1], WIFI_SSID_MAX);
    wifi_ssid[WIFI_SSID_MAX] = '\0';

    rc = settings_save();
    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
        return rc;
    }

    shell_print(sh, "WiFi SSID saved: '%s'", wifi_ssid);
    return 0;
}

/*
 * Shell command: set WiFi password
 */
static int cmd_wifi_set_password(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (argc != 2) {
        shell_error(sh, "Usage: set_password <password>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > WIFI_PSK_MAX) {
        shell_error(sh, "Password too long (max %d)", WIFI_PSK_MAX);
        return -EINVAL;
    }

    strncpy(wifi_psk, argv[1], WIFI_PSK_MAX);
    wifi_psk[WIFI_PSK_MAX] = '\0';
    wifi_credentials_set = true;

    rc = settings_save();
    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
        return rc;
    }

    shell_print(sh, "WiFi password saved");
    return 0;
}

/*
 * Start HTTP configuration server
 */
static void start_http_server(void)
{
    int rc;

    /* Check if already running */
    if (http_srv.state == HTTP_SERVER_RUNNING) {
        printk("HTTP server already running\n");
        return;
    }

    /* Wait for DHCP to complete and get IP address */
    printk("Waiting for IP address...\n");
    k_msleep(3000);

    /* Start HTTP server for web configuration interface */
    printk("Starting HTTP configuration server...\n");

    /* Initialize WiFi scanner if not already done */
    if (scanner.state == WIFI_SCANNER_IDLE) {
        rc = wifi_scanner_init(&scanner);
        if (rc) {
            printk("Warning: WiFi scanner init failed: %d\n", rc);
        }
    }

    /* Initialize HTTP server */
    rc = http_server_init(&http_srv, &scanner);
    if (rc == 0) {
        rc = http_server_start(&http_srv, provisioning_creds_received, NULL);
        if (rc == 0) {
            struct net_if *iface = net_if_get_default();
            if (iface && iface->config.ip.ipv4 &&
                iface->config.ip.ipv4->unicast[0].ipv4.addr_state == NET_ADDR_PREFERRED) {
                struct in_addr *addr = &iface->config.ip.ipv4->unicast[0].ipv4.address.in_addr;
                printk("\n");
                printk("===========================================\n");
                printk("  WiFi Configuration Interface Ready\n");
                printk("===========================================\n");
                printk("  Open browser to: http://%d.%d.%d.%d\n",
                       addr->s4_addr[0], addr->s4_addr[1],
                       addr->s4_addr[2], addr->s4_addr[3]);
                printk("===========================================\n\n");
            } else {
                printk("HTTP server started (waiting for IP address)\n");
                printk("Use 'net iface' to check IP address\n");
            }
        } else {
            printk("Warning: Failed to start HTTP server: %d\n", rc);
        }
    } else {
        printk("Warning: Failed to init HTTP server: %d\n", rc);
    }
}

/*
 * Shell command: connect to WiFi
 */
static int cmd_wifi_connect(const struct shell *sh, size_t argc, char **argv)
{
    int rc = wifi_connect_stored();
    if (rc) {
        shell_error(sh, "WiFi connection failed: %d", rc);
        return rc;
    }

    shell_print(sh, "WiFi connected successfully");

    /* Start HTTP server after successful connection */
    start_http_server();

    return 0;
}

/*
 * Shell command: reset WiFi credentials
 */
static int cmd_wifi_reset(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    shell_print(sh, "Resetting WiFi credentials...");

    /* Clear in-memory variables */
    memset(wifi_ssid, 0, sizeof(wifi_ssid));
    memset(wifi_psk, 0, sizeof(wifi_psk));
    wifi_credentials_set = false;

    /* Delete from persistent storage */
    rc = settings_delete("demo/wifi_ssid");
    if (rc && rc != -ENOENT) {
        shell_error(sh, "Failed to delete SSID: %d", rc);
    }

    rc = settings_delete("demo/wifi_psk");
    if (rc && rc != -ENOENT) {
        shell_error(sh, "Failed to delete password: %d", rc);
    }

    /* Save to persist deletion */
    rc = settings_save();
    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
        return rc;
    }

    shell_print(sh, "WiFi credentials cleared successfully");
    shell_print(sh, "Device will enter provisioning mode on next boot");
    return 0;
}

/*
 * Shell command: show WiFi status
 */
static int cmd_wifi_status(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "WiFi Status:");
    shell_print(sh, "  SSID: %s", strlen(wifi_ssid) > 0 ? wifi_ssid : "<not set>");
    shell_print(sh, "  Password: %s", strlen(wifi_psk) > 0 ? "***" : "<not set>");
    shell_print(sh, "  Connected: %s", wifi_connected ? "Yes" : "No");
    return 0;
}

/*
 * Shell command: show all settings
 */
static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Settings:");
    shell_print(sh, "  Boot count: %u", boot_count);
    shell_print(sh, "  WiFi SSID: %s", strlen(wifi_ssid) > 0 ? wifi_ssid : "<not set>");
    shell_print(sh, "  WiFi Password: %s", strlen(wifi_psk) > 0 ? "***" : "<not set>");
    shell_print(sh, "  WiFi Connected: %s", wifi_connected ? "Yes" : "No");
    return 0;
}

/* Register WiFi shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(wifi_cmds,
    SHELL_CMD(set_ssid, NULL, "Set WiFi SSID", cmd_wifi_set_ssid),
    SHELL_CMD(set_password, NULL, "Set WiFi password", cmd_wifi_set_password),
    SHELL_CMD(connect, NULL, "Connect to WiFi", cmd_wifi_connect),
    SHELL_CMD(reset, NULL, "Clear WiFi credentials", cmd_wifi_reset),
    SHELL_CMD(status, NULL, "Show WiFi status", cmd_wifi_status),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(wifi, &wifi_cmds, "WiFi commands", NULL);

/* Register demo shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(demo_cmds,
    SHELL_CMD(show, NULL, "Show all settings", cmd_show),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(demo, &demo_cmds, "Settings demo commands", NULL);

/*
 * Provisioning credentials callback
 *
 * Called when user submits WiFi credentials via AP provisioning interface
 */
static void provisioning_creds_received(const char *ssid,
                                         const char *password,
                                         void *user_data)
{
	ARG_UNUSED(user_data);

	printk("\n=== New WiFi Credentials Received ===\n");
	printk("SSID: %s\n", ssid);
	printk("Password: ***\n");

	/* Store new credentials */
	if (strlen(ssid) <= WIFI_SSID_MAX) {
		strncpy(wifi_ssid, ssid, WIFI_SSID_MAX);
		wifi_ssid[WIFI_SSID_MAX] = '\0';
	}

	if (strlen(password) <= WIFI_PSK_MAX) {
		strncpy(wifi_psk, password, WIFI_PSK_MAX);
		wifi_psk[WIFI_PSK_MAX] = '\0';
		wifi_credentials_set = true;
	}

	/* Save to flash */
	int rc = settings_save();
	if (rc) {
		printk("Warning: Failed to save credentials: %d\n", rc);
	} else {
		printk("Credentials saved to flash\n");
	}

	/* Stop provisioning mode */
	printk("Stopping provisioning mode...\n");
	http_server_stop(&http_srv);
	wifi_ap_provisioning_stop(&ap_prov);
	provisioning_mode = false;

	/* Wait for AP to fully stop before attempting station mode */
	printk("Waiting for AP to shut down...\n");
	k_msleep(2000);

	/* Try to connect to new network */
	printk("Attempting to connect to new network...\n");
	wifi_connect_stored();
}

/*
 * Start AP provisioning mode
 *
 * Creates an access point and HTTP server for WiFi configuration
 */
static int start_provisioning_mode(void)
{
	int rc;

	if (provisioning_mode) {
		printk("Already in provisioning mode\n");
		return 0;
	}

	printk("\n=== Entering Provisioning Mode ===\n");

	/* Initialize WiFi scanner */
	rc = wifi_scanner_init(&scanner);
	if (rc) {
		printk("ERROR: WiFi scanner init failed: %d\n", rc);
		return rc;
	}

	/* Scan for networks to show in web interface */
	printk("Scanning for WiFi networks...\n");
	rc = wifi_scanner_scan(&scanner, 10000);
	if (rc) {
		printk("Warning: WiFi scan failed: %d\n", rc);
	} else {
		size_t count;
		wifi_scanner_get_results(&scanner, &count);
		printk("Found %zu networks\n", count);
	}

	/* Initialize HTTP server */
	rc = http_server_init(&http_srv, &scanner);
	if (rc) {
		printk("ERROR: HTTP server init failed: %d\n", rc);
		return rc;
	}

	/* Initialize AP provisioning */
	rc = wifi_ap_provisioning_init(&ap_prov, NULL);
	if (rc) {
		printk("ERROR: AP provisioning init failed: %d\n", rc);
		return rc;
	}

	/* Note: AP mode support on Pico W is limited in Zephyr.
	 * For now, provisioning via serial shell is the primary method.
	 * Future: Add BLE provisioning or use external AP hardware.
	 */

	/* Start AP - may not be fully supported on CYW43439 */
	rc = wifi_ap_provisioning_start(&ap_prov, provisioning_creds_received, NULL);
	if (rc) {
		printk("WARNING: AP mode not available: %d\n", rc);
		printk("Use shell commands for WiFi configuration:\n");
		printk("  wifi set_ssid <ssid>\n");
		printk("  wifi set_password <pass>\n");
		printk("  wifi connect\n");
		/* Don't fail - allow shell configuration */
		provisioning_mode = false;
		return 0;
	}

	/* Start HTTP server */
	rc = http_server_start(&http_srv, provisioning_creds_received, NULL);
	if (rc) {
		printk("ERROR: Failed to start HTTP server: %d\n", rc);
		wifi_ap_provisioning_stop(&ap_prov);
		return rc;
	}

	provisioning_mode = true;

	printk("\n");
	printk("===========================================\n");
	printk("  WiFi Provisioning Mode Active\n");
	printk("===========================================\n");
	printk("1. Connect to WiFi network: %s\n", WIFI_AP_DEFAULT_SSID);
	printk("2. Open browser to: http://%s\n", WIFI_AP_DEFAULT_IP);
	printk("3. Select your WiFi network and enter password\n");
	printk("===========================================\n\n");

	return 0;
}

/*
 * Main application
 */
int main(void)
{
    int rc;
    const struct flash_area *fa;
    const struct device *flash_dev;

    printk("\n=== Settings Demo ===\n");
    printk("Board: Raspberry Pi Pico W\n\n");

    /* Verify flash device is ready */
    rc = flash_area_open(STORAGE_PARTITION_ID, &fa);
    if (rc) {
        printk("ERROR: Failed to open storage partition: %d\n", rc);
        return rc;
    }

    flash_dev = fa->fa_dev;
    if (!device_is_ready(flash_dev)) {
        printk("ERROR: Flash device not ready\n");
        flash_area_close(fa);
        return -ENODEV;
    }

    printk("Flash storage ready (offset=0x%lx size=0x%lx)\n",
           (unsigned long)fa->fa_off, (unsigned long)fa->fa_size);
    flash_area_close(fa);

    /* Initialize settings subsystem */
    rc = settings_subsys_init();
    if (rc) {
        printk("ERROR: Settings initialization failed: %d\n", rc);
        return rc;
    }

    /* Load existing settings from flash */
    rc = settings_load();
    if (rc) {
        printk("Warning: Settings load returned %d\n", rc);
    }

    /* Increment and save boot counter */
    boot_count++;
    printk("\nBoot count: %u\n", boot_count);

    rc = settings_save();
    if (rc) {
        printk("Warning: Failed to save boot count: %d\n", rc);
    }

    /* Initialize WiFi management callbacks */
    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT |
                                 NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    /* Initialize extended WiFi shell commands */
    wifi_shell_commands_init(&scanner, &ap_prov);

    /* Auto-connect to WiFi if credentials are stored */
    if (strlen(wifi_ssid) > 0 && wifi_credentials_set) {
        printk("\nAuto-connecting to WiFi...\n");

        /* Wait for WiFi subsystem to be fully ready */
        k_msleep(2000);

        rc = wifi_connect_stored();
        if (rc == 0) {
            printk("Auto-connect successful\n");
            start_http_server();
        } else {
            printk("Auto-connect failed (use 'wifi connect' to retry)\n");
        }
    } else {
        printk("\nNo WiFi credentials stored.\n");
        printk("Entering AP provisioning mode...\n");

        /* Start provisioning mode if no credentials */
        rc = start_provisioning_mode();
        if (rc) {
            printk("ERROR: Failed to start provisioning mode: %d\n", rc);
            printk("Use shell commands to configure WiFi manually\n");
        }
    }

    /* Display available commands */
    printk("\nShell commands:\n");
    printk("  wifi set_ssid <ssid>      - Store WiFi SSID\n");
    printk("  wifi set_password <pass>  - Store WiFi password\n");
    printk("  wifi connect              - Connect to WiFi\n");
    printk("  wifi status               - Show WiFi status\n");
    printk("  wifi_ext reset            - Clear WiFi credentials\n");
    printk("  wifi_ext scan             - Scan for networks\n");
    printk("  wifi_ext provision        - Start provisioning mode\n");
    printk("  demo show                 - Show all settings\n");
    printk("  kernel reboot             - Test persistence\n\n");

    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
