#ifndef H_SMP_BT_
#define H_SMP_BT_

#include <zephyr/types.h>
struct bt_conn;

int smp_bt_register(void);
int smp_bt_tx_rsp(struct bt_conn *conn, const void *data, u16_t len);

#endif
