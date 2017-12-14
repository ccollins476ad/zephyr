#ifndef H_ZEPHYR_NMGR_
#define H_ZEPHYR_NMGR_

struct zephyr_nmgr_pkt;

int zephyr_nmgr_process_packet(struct zephyr_nmgr_pkt *pkt);

#endif
