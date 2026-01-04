/*
 * settingsdev - minimal Zephyr settings/NVS smoketest for Raspberry Pi Pico W
 *
 * What it does:
 *  - Confirms the chosen settings partition exists (flash_area_open)
 *  - Initialises the settings subsystem (NVS backend)
 *  - Loads settings (invokes handler)
 *  - Writes demo/value on first boot, increments it on subsequent boots
 *
 * Target board:
 *  - rpi_pico/rp2040/w
 *
 * Requires:
 *  - boards/rpi_pico_rp2040_w.overlay defines storage_partition and
 *    chosen zephyr,settings-partition = &storage_partition
 *  - prj.conf enables CONFIG_SETTINGS + CONFIG_SETTINGS_NVS + CONFIG_NVS
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>

#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(picosettings, LOG_LEVEL_INF);

/* Flash area ID derived from the devicetree 'chosen' settings partition */
#define SETTINGS_AREA_ID DT_FIXED_PARTITION_ID(DT_CHOSEN(zephyr_settings_partition))

/* Store a single uint32_t under "demo/value" */
static uint32_t demo_value;
static bool demo_value_loaded;

/*
 * Settings handler: called during settings_load() for keys under subtree "demo".
 * name is relative to "demo", e.g. "value".
 */
static int demo_settings_set(const char *name, size_t len,
                             settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "value") != 0) {
        return -ENOENT;
    }

    if (len != sizeof(demo_value)) {
        LOG_WRN("demo/value wrong size: %u", (unsigned)len);
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, &demo_value, sizeof(demo_value));
    if (rc < 0) {
        LOG_ERR("read_cb failed: %d", rc);
        return rc;
    }

    demo_value_loaded = true;
    LOG_INF("Loaded demo/value=%u from settings", (unsigned)demo_value);
    return 0;
}

/* Register the handler for subtree "demo" */
SETTINGS_STATIC_HANDLER_DEFINE(demo_handler,
                               "demo",
                               NULL,               /* h_get */
                               demo_settings_set,   /* h_set */
                               NULL,               /* h_commit */
                               NULL);              /* h_export */

static void settings_smoketest(void)
{
    const struct flash_area *fa;
    int rc;

    /* Confirm the settings partition exists and is accessible */
    rc = flash_area_open(SETTINGS_AREA_ID, &fa);
    if (rc) {
        LOG_ERR("flash_area_open(settings) failed: %d", rc);
        return;
    }

    LOG_INF("storage flash area: off=0x%x size=0x%x",
            (unsigned)fa->fa_off, (unsigned)fa->fa_size);

    flash_area_close(fa);

    /* Init settings subsystem (binds to NVS backend) */
    rc = settings_subsys_init();
    LOG_INF("settings_subsys_init rc=%d", rc);
    if (rc) {
        return;
    }

    /* Load settings (invokes our handler if key exists) */
    demo_value_loaded = false;
    rc = settings_load();
    LOG_INF("settings_load rc=%d", rc);
    if (rc) {
        return;
    }

    if (!demo_value_loaded) {
        demo_value = 1;
        rc = settings_save_one("demo/value", &demo_value, sizeof(demo_value));
        LOG_INF("demo/value not found, saved: %u (rc=%d)", (unsigned)demo_value, rc);
    } else {
        demo_value++;
        rc = settings_save_one("demo/value", &demo_value, sizeof(demo_value));
        LOG_INF("incremented demo/value to %u (rc=%d)", (unsigned)demo_value, rc);
    }
}

int main(void)
{
    LOG_INF("Boot: picosettings settings smoketest");

    settings_smoketest();

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
