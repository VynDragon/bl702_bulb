SECTION_PROLOGUE(.itcm_noinit, (NOLOAD),)
{
	. = ALIGN(16);
	*:freertos_shim.c.obj(.noinit .noinit.*)
	*:hci_bflb.c.obj(.noinit .noinit.*)
	*:shell_uart.c.obj(.noinit .noinit.*)
	*:net_pkt.c.obj(.noinit .noinit.*)
	*:buf.c.obj(.noinit .noinit.*)
	. = ALIGN(4);
} GROUP_NOLOAD_LINK_IN(ITCM, ITCM)
