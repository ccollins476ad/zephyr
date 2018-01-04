#ifndef H_SMP_BT_
#define H_SMP_BT_

#include <zephyr/types.h>
struct bt_conn;

typedef void (*smp_bt_cb)(struct bt_conn *conn, const u8_t *buf, size_t len);

int smp_bt_register(smp_bt_cb cb);
int smp_bt_tx_rsp(struct bt_conn *conn, const void *data, u16_t len);

#endif
