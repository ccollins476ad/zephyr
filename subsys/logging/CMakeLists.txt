zephyr_sources_ifdef(CONFIG_SYS_LOG sys_log.c)
zephyr_sources_ifdef(
  CONFIG_KERNEL_EVENT_LOGGER
  event_logger.c
  kernel_event_logger.c
  )

zephyr_sources_ifdef(CONFIG_MDLOG mdlog.c)
zephyr_sources_ifdef(CONFIG_MDLOG_FCB mdlog_fcb.c)
zephyr_sources_ifdef(CONFIG_MDLOG_CONSOLE mdlog_console.c)
zephyr_sources_ifdef(CONFIG_REBOOT_LOG reboot_log.c)
