/*
 * lwipopts.h — lwIP configuration for Mellivora OS PicoCalc (Pico 2W)
 *
 * Tuned for a memory-constrained embedded shell with basic TCP/UDP
 * networking over CYW43 WiFi.
 */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

/* No RTOS — we use polling or threadsafe_background */
#define NO_SYS                      1

/* Core protocol features */
#define LWIP_DHCP                   1
#define LWIP_DNS                    1
#define LWIP_RAW                    1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_ICMP                   1
#define LWIP_IGMP                   0
#define LWIP_ARP                    1

/* No sockets/netconn API — we use raw lwIP callbacks */
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0

/* Hostname and status callbacks for DHCP */
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  1

/* Memory tuning for RP2350 (520KB RAM, ~150KB free) */
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_TCP_SEG            16
#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1536

/* TCP tuning */
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)

/* DNS */
#define LWIP_DNS_MAX_SERVERS        2
#define DNS_MAX_NAME_LENGTH         128

/* SNTP for NTP time sync */
#define SNTP_SERVER_DNS             1
#define SNTP_MAX_SERVERS            2

/* Timeouts */
#define DHCP_FINE_TIMER_MSECS       500

/* Stats — disabled to save RAM */
#define LWIP_STATS                  0
#define LWIP_STATS_DISPLAY          0

/* Checksum offload — CYW43 handles checksums */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1

#endif /* _LWIPOPTS_H */
