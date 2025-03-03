
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/sys/util.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bt_app);

#ifdef CONFIG_BT
#define BT_UUID_WRITE_SERVICE \
BT_UUID_128_ENCODE(0x6E400001, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)

#define BT_UUID_WRITE_VAL \
BT_UUID_128_ENCODE(0x6E400002, 0xB5A3, 0xF393, 0xE0A9, 0xE50E24DCCA9E)

#define BT_MTU_UPDATE_SERVICE BT_UUID_128_ENCODE(0x2e2b8dc3, 0x06e0, 0x4f93, 0x9bb2, 0x734091c356f0)

/* Overhead: opcode (u8) + handle (u16) */
#define ATT_NTF_SIZE(payload_len) (1 + 2 + payload_len)

static struct bt_uuid_128 write_service_uuid = BT_UUID_INIT_128(BT_UUID_WRITE_SERVICE);
static struct bt_uuid_128 write_uuid = BT_UUID_INIT_128(BT_UUID_WRITE_VAL);
static const struct bt_uuid_128 mtu_update_service = BT_UUID_INIT_128(BT_MTU_UPDATE_SERVICE);

static const struct bt_uuid_128 notify_characteristic_uuid =
	BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x2e2b8dc3, 0x06e0, 0x4f93, 0x9bb2, 0x734091c356f3));

uint8_t rx_data[248];
bool data_received = false;
static struct bt_conn *default_conn;

static ssize_t write_handler(struct bt_conn *conn, const struct bt_gatt_attr *attr,
	const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{

	LOG_INF("write_handler: len %d, offset %d", len, offset);
	if (offset > 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (len > sizeof(rx_data)) {
		LOG_INF("write_handler: invalid len %d", len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(rx_data, buf, len);
	rx_data[len] = '\0';
	data_received = true;

	LOG_INF("Received data: %s", rx_data);
	return len;
}

BT_GATT_SERVICE_DEFINE(write_service, BT_GATT_PRIMARY_SERVICE(&write_service_uuid),
BT_GATT_CHARACTERISTIC(&write_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE,
			            NULL, write_handler, NULL),
);

static const struct bt_data ad[] = {
BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_MTU_UPDATE_SERVICE),
BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

	printk("MTU Test Update: notifications %s\n", notif_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(mtu_UPDATE, BT_GATT_PRIMARY_SERVICE(&mtu_update_service),
		       BT_GATT_CHARACTERISTIC(&notify_characteristic_uuid.uuid, BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_NONE, NULL, NULL, NULL),
		       BT_GATT_CCC(ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
		return;
	}
	default_conn = bt_conn_ref(conn);
	LOG_INF("Connected");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected (reason %u)", reason);

	bt_conn_unref(conn);
	default_conn = NULL;
	bt_le_adv_stop();

    int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Restart advertising failed (err %d)\n", err);
    } else {
        printk("Advertising restarted\n");
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int bt_init(void)
{
    int err;

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return -1;
    }

	printk("Bluetooth initialized\n");

    bt_gatt_cb_register(&gatt_callbacks);
	struct bt_gatt_attr *notify_crch =
		bt_gatt_find_by_uuid(mtu_UPDATE.attrs, 0xffff, &notify_characteristic_uuid.uuid);

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
        return -1;
    }

	printk("Advertising successfully started\n");

    uint8_t notify_data[100] = {};

	notify_data[13] = 0x7f;
	notify_data[99] = 0x55;

    k_sleep(K_SECONDS(1));
    /* Only send the notification if the UATT MTU supports the required length */
    if (bt_gatt_get_uatt_mtu(default_conn) >= ATT_NTF_SIZE(sizeof(notify_data))) {
        bt_gatt_notify(default_conn, notify_crch, notify_data, sizeof(notify_data));
    } else {
        printk("Skipping notification since UATT MTU is not sufficient."
                "Required: %d, Actual: %d\n",
                ATT_NTF_SIZE(sizeof(notify_data)),
                bt_gatt_get_uatt_mtu(default_conn));
    }

    return 0;
}
#endif