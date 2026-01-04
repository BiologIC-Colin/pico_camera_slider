/*
 * WiFi Settings Demo for Raspberry Pi Pico W
 *
 * Demonstrates persistent storage using Zephyr Settings API with ZMS backend.
 *
 * Features:
 * - Boot counter that increments on each reboot
 * - WiFi credential storage (SSID and password)
 * - Automatic WiFi connection on boot if credentials are stored
 * - Persistent storage survives power cycles
 *
 * Shell Commands:
 *   wifi set_ssid <ssid>      - Store WiFi SSID
 *   wifi set_password <pass>  - Store WiFi password
 *   wifi connect              - Connect to WiFi using stored credentials
 *   wifi status               - Show WiFi connection status
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
#include <string.h>

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
                                    uint32_t mgmt_event, struct net_if *iface)
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

    /* Auto-connect to WiFi if credentials are stored */
    if (strlen(wifi_ssid) > 0 && wifi_credentials_set) {
        printk("\nAuto-connecting to WiFi...\n");
        rc = wifi_connect_stored();
        if (rc == 0) {
            printk("Auto-connect successful\n");
        } else {
            printk("Auto-connect failed (use 'wifi connect' to retry)\n");
        }
    } else {
        printk("\nNo WiFi credentials stored. Use 'wifi set_ssid' and 'wifi set_password'.\n");
    }

    /* Display available commands */
    printk("\nShell commands:\n");
    printk("  wifi set_ssid <ssid>      - Store WiFi SSID\n");
    printk("  wifi set_password <pass>  - Store WiFi password\n");
    printk("  wifi connect              - Connect to WiFi\n");
    printk("  wifi status               - Show WiFi status\n");
    printk("  demo show                 - Show all settings\n");
    printk("  kernel reboot             - Test persistence\n\n");

    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
