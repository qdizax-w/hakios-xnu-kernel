/*
 * What ath10k's qmi.c takes of Linux's sockets: the address type it casts its QRTR address to for kernel_connect(),
 * which here is the kernel's QMI helper's (bsd/sys/hakios.qmi.h) and takes any address. Nothing else of sockets is used.
 */
#ifndef _LX_NET_SOCK_H_
#define _LX_NET_SOCK_H_

/* (not linux/socket.h: the kernel's QMI header has the socket calls its own way) */
#include <linux/delay.h>

struct sockaddr_unsized {
	unsigned short sa_family;
	char sa_data[];
};

#endif
