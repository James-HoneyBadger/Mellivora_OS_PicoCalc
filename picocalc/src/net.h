/*
 * net.h — Network abstraction for Mellivora OS PicoCalc (Pico 2W)
 *
 * Provides WiFi management, DNS resolution, TCP client, raw ICMP ping,
 * NTP time sync, and HTTP GET — all built on CYW43 + lwIP.
 *
 * Only compiled when PICO_CYW43_SUPPORTED is defined (pico2_w board).
 */

#pragma once

#ifdef PICO_CYW43_SUPPORTED

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ---- WiFi management ---- */
int  net_init(void);
bool net_is_connected(void);
int  net_wifi_connect(const char *ssid, const char *password, int timeout_ms);
void net_wifi_disconnect(void);

/* Scan callback: called once per AP found */
typedef void (*net_scan_cb_t)(const char *ssid, int rssi, int auth, void *ctx);
int  net_wifi_scan(net_scan_cb_t cb, void *ctx);

/* ---- IP info ---- */
typedef struct {
    uint32_t ip;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns;
} net_ifinfo_t;

int  net_get_ifinfo(net_ifinfo_t *info);
void net_ip_to_str(uint32_t ip, char *buf, size_t sz);

/* ---- DNS ---- */
int  net_dns_resolve(const char *hostname, uint32_t *ip_out, int timeout_ms);

/* ---- Ping (ICMP echo) ---- */
int  net_ping(uint32_t ip, int count, int timeout_ms);

/* ---- NTP time sync ---- */
int  net_ntp_sync(const char *server, int timeout_ms);
bool net_ntp_valid(void);
void net_ntp_time(int *hour, int *min, int *sec);
void net_ntp_date(int *year, int *month, int *day);

/* ---- TCP client ---- */
typedef struct net_tcp net_tcp_t;
net_tcp_t *net_tcp_connect(uint32_t ip, uint16_t port, int timeout_ms);
int        net_tcp_send(net_tcp_t *conn, const void *data, size_t len);
int        net_tcp_recv(net_tcp_t *conn, void *buf, size_t cap, int timeout_ms);
void       net_tcp_close(net_tcp_t *conn);

/* ---- HTTP GET (simple) ---- */
int  net_http_get(const char *host, uint16_t port, const char *path,
                  char *buf, size_t cap, int timeout_ms);

/* ---- Poll (call periodically to service lwIP) ---- */
void net_poll(void);

#endif /* PICO_CYW43_SUPPORTED */
