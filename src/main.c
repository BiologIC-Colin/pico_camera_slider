/*
 * Settings Demo for Raspberry Pi Pico W
 *
 * Demonstrates persistent storage using Zephyr Settings API with ZMS backend.
 *
 * Features:
 * - Boot counter that increments on each reboot
 * - String storage with shell commands
 * - Persistent storage survives power cycles
 *
 * Shell Commands:
 *   demo set_string <text>  - Store a string in persistent storage
 *   demo show               - Display current settings
 *   demo save               - Manually save all settings to flash
 *   demo load               - Reload settings from flash
 *   kernel reboot           - Reboot to test persistence
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/device.h>
#include <string.h>

#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(storage_partition)

/* Settings values */
static uint32_t boot_count = 0;
#define USER_STRING_MAX 64
static char user_string[USER_STRING_MAX] = "";

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

    if (strcmp(name, "user_string") == 0) {
        if (len >= USER_STRING_MAX) {
            return -EINVAL;
        }
        rc = read_cb(cb_arg, user_string, len);
        if (rc >= 0) {
            user_string[len] = '\0';
            printk("Loaded user_string = '%s'\n", user_string);
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

    if (strlen(user_string) > 0) {
        (void)cb("demo/user_string", user_string, strlen(user_string));
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
 * Shell command: set string
 */
static int cmd_set_string(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (argc != 2) {
        shell_error(sh, "Usage: set_string <text>");
        return -EINVAL;
    }

    if (strlen(argv[1]) >= USER_STRING_MAX) {
        shell_error(sh, "String too long (max %d)", USER_STRING_MAX - 1);
        return -EINVAL;
    }

    strncpy(user_string, argv[1], USER_STRING_MAX - 1);
    user_string[USER_STRING_MAX - 1] = '\0';

    rc = settings_save();
    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
        return rc;
    }

    shell_print(sh, "String saved: '%s'", user_string);
    return 0;
}

/*
 * Shell command: show settings
 */
static int cmd_show(const struct shell *sh, size_t argc, char **argv)
{
    shell_print(sh, "Settings:");
    shell_print(sh, "  Boot count: %u", boot_count);
    shell_print(sh, "  User string: %s",
                strlen(user_string) > 0 ? user_string : "<empty>");
    return 0;
}

/*
 * Shell command: save all settings
 */
static int cmd_save(const struct shell *sh, size_t argc, char **argv)
{
    int rc = settings_save();
    if (rc) {
        shell_error(sh, "Failed to save: %d", rc);
        return rc;
    }

    shell_print(sh, "Settings saved successfully");
    return 0;
}

/*
 * Shell command: reload settings from flash
 */
static int cmd_load(const struct shell *sh, size_t argc, char **argv)
{
    int rc = settings_load();
    if (rc) {
        shell_error(sh, "Failed to load: %d", rc);
        return rc;
    }

    shell_print(sh, "Settings reloaded:");
    shell_print(sh, "  Boot count: %u", boot_count);
    shell_print(sh, "  User string: %s",
                strlen(user_string) > 0 ? user_string : "<empty>");
    return 0;
}

/* Register shell commands */
SHELL_STATIC_SUBCMD_SET_CREATE(demo_cmds,
    SHELL_CMD(set_string, NULL, "Set user string", cmd_set_string),
    SHELL_CMD(show, NULL, "Show current settings", cmd_show),
    SHELL_CMD(save, NULL, "Save settings to flash", cmd_save),
    SHELL_CMD(load, NULL, "Load settings from flash", cmd_load),
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

    /* Display available commands */
    printk("\nShell commands:\n");
    printk("  demo set_string <text>  - Store a string\n");
    printk("  demo show               - Show current settings\n");
    printk("  demo save               - Save all settings\n");
    printk("  demo load               - Reload from flash\n");
    printk("  kernel reboot           - Test persistence\n\n");

    /* Main loop */
    while (1) {
        k_sleep(K_SECONDS(10));
    }

    return 0;
}
