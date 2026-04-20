/*
 * net.c — Network layer for Mellivora OS PicoCalc (Pico 2W)
 *
 * WiFi + lwIP networking: connect/scan/ping/DNS/NTP/TCP/HTTP.
 * Only compiled when PICO_CYW43_SUPPORTED is defined.
 */

#ifdef PICO_CYW43_SUPPORTED

#include "net.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include <string.h>
#include <stdio.h>

/* ============================================================
 * State
 * ============================================================ */
static bool _net_inited = false;
static bool _wifi_connected = false;

/* NTP state */
static bool     _ntp_valid = false;
static uint64_t _ntp_epoch_sec = 0;       /* UTC epoch at sync moment */
static uint64_t _ntp_sync_us  = 0;        /* time_us_64() at sync */

/* ============================================================
 * Init / poll
 * ============================================================ */
int net_init(void) {
    if (_net_inited) return 0;
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_WORLDWIDE)) return -1;
    cyw43_arch_enable_sta_mode();
    _net_inited = true;
    return 0;
}

void net_poll(void) {
    if (_net_inited) {
        cyw43_arch_poll();
        sys_check_timeouts();
    }
}

/* ============================================================
 * WiFi connect / disconnect
 * ============================================================ */
bool net_is_connected(void) {
    if (!_net_inited) return false;
    return _wifi_connected &&
           (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP);
}

int net_wifi_connect(const char *ssid, const char *password, int timeout_ms) {
    if (!_net_inited) { if (net_init() < 0) return -1; }

    int rc = cyw43_arch_wifi_connect_timeout_ms(ssid, password,
                CYW43_AUTH_WPA2_AES_PSK, (uint32_t)timeout_ms);
    if (rc == 0) {
        _wifi_connected = true;
        return 0;
    }
    /* Retry with open auth if WPA2 failed */
    if (password == NULL || password[0] == '\0') {
        rc = cyw43_arch_wifi_connect_timeout_ms(ssid, NULL,
                    CYW43_AUTH_OPEN, (uint32_t)timeout_ms);
        if (rc == 0) { _wifi_connected = true; return 0; }
    }
    return -1;
}

void net_wifi_disconnect(void) {
    if (_net_inited) {
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        _wifi_connected = false;
    }
}

/* ============================================================
 * WiFi scan
 * ============================================================ */
typedef struct {
    net_scan_cb_t cb;
    void         *ctx;
} _scan_ctx_t;

static int _scan_result_cb(void *env, const cyw43_ev_scan_result_t *result) {
    _scan_ctx_t *sc = (_scan_ctx_t *)env;
    if (result && sc->cb) {
        char ssid_buf[33];
        size_t len = result->ssid_len;
        if (len > 32) len = 32;
        memcpy(ssid_buf, result->ssid, len);
        ssid_buf[len] = '\0';

        int auth = 0;
        if (result->auth_mode & 0x02) auth = 2;  /* WPA */
        if (result->auth_mode & 0x04) auth = 3;  /* WPA2 */

        sc->cb(ssid_buf, result->rssi, auth, sc->ctx);
    }
    return 0;
}

int net_wifi_scan(net_scan_cb_t cb, void *ctx) {
    if (!_net_inited) { if (net_init() < 0) return -1; }

    _scan_ctx_t sc = { .cb = cb, .ctx = ctx };
    cyw43_wifi_scan_options_t opts = {0};
    int rc = cyw43_wifi_scan(&cyw43_state, &opts, &sc, _scan_result_cb);
    if (rc != 0) return -1;

    /* Poll until scan completes (up to 10 seconds) */
    absolute_time_t deadline = make_timeout_time_ms(10000);
    while (cyw43_wifi_scan_active(&cyw43_state)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) break;
        net_poll();
        sleep_ms(50);
    }
    return 0;
}

/* ============================================================
 * IP info
 * ============================================================ */
int net_get_ifinfo(net_ifinfo_t *info) {
    if (!info) return -1;
    struct netif *nif = netif_list;
    if (!nif) return -1;
    info->ip      = ip4_addr_get_u32(netif_ip4_addr(nif));
    info->netmask = ip4_addr_get_u32(netif_ip4_netmask(nif));
    info->gateway = ip4_addr_get_u32(netif_ip4_gw(nif));
    const ip_addr_t *dns_addr = dns_getserver(0);
    info->dns = dns_addr ? ip4_addr_get_u32(ip_2_ip4(dns_addr)) : 0;
    return 0;
}

void net_ip_to_str(uint32_t ip, char *buf, size_t sz) {
    snprintf(buf, sz, "%lu.%lu.%lu.%lu",
             (unsigned long)(ip & 0xFF),
             (unsigned long)((ip >> 8) & 0xFF),
             (unsigned long)((ip >> 16) & 0xFF),
             (unsigned long)((ip >> 24) & 0xFF));
}

/* ============================================================
 * DNS resolution
 * ============================================================ */
typedef struct {
    ip_addr_t addr;
    bool      done;
    bool      ok;
} _dns_ctx_t;

static void _dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    _dns_ctx_t *dc = (_dns_ctx_t *)arg;
    if (ipaddr) {
        dc->addr = *ipaddr;
        dc->ok = true;
    }
    dc->done = true;
}

int net_dns_resolve(const char *hostname, uint32_t *ip_out, int timeout_ms) {
    if (!hostname || !ip_out) return -1;

    _dns_ctx_t dc = { .done = false, .ok = false };
    ip_addr_t cached;
    err_t err = dns_gethostbyname(hostname, &cached, _dns_cb, &dc);
    if (err == ERR_OK) {
        *ip_out = ip4_addr_get_u32(ip_2_ip4(&cached));
        return 0;
    }
    if (err != ERR_INPROGRESS) return -1;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!dc.done) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) return -2;
        net_poll();
        sleep_ms(10);
    }
    if (!dc.ok) return -1;
    *ip_out = ip4_addr_get_u32(ip_2_ip4(&dc.addr));
    return 0;
}

/* ============================================================
 * Ping (ICMP echo)
 * ============================================================ */
static volatile bool _ping_reply;
static volatile int  _ping_time_ms;
static uint16_t      _ping_seq;

static uint8_t _ping_recv_cb(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr) {
    (void)arg; (void)pcb; (void)addr;
    /* Check ICMP echo reply */
    if (p->tot_len >= 28) { /* IP header(20) + ICMP header(8) */
        uint8_t *data = (uint8_t *)p->payload;
        /* Skip IP header (usually 20 bytes) */
        int ihl = (data[0] & 0x0F) * 4;
        if (p->tot_len >= (uint16_t)(ihl + 8)) {
            uint8_t type = data[ihl];
            if (type == 0) { /* ICMP Echo Reply */
                _ping_reply = true;
            }
        }
    }
    pbuf_free(p);
    return 1;
}

int net_ping(uint32_t ip, int count, int timeout_ms) {
    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (!pcb) return -1;

    raw_recv(pcb, _ping_recv_cb, NULL);
    raw_bind(pcb, IP4_ADDR_ANY);

    ip_addr_t dest;
    ip4_addr_set_u32(ip_2_ip4(&dest), ip);
    IP_SET_TYPE(&dest, IPADDR_TYPE_V4);

    int ok_count = 0;
    _ping_seq = 0;

    for (int i = 0; i < count; i++) {
        _ping_reply = false;
        _ping_seq++;

        /* Build ICMP echo request */
        struct pbuf *pb = pbuf_alloc(PBUF_IP, 64, PBUF_RAM);
        if (!pb) continue;

        uint8_t *icmp = (uint8_t *)pb->payload;
        memset(icmp, 0, 64);
        icmp[0] = 8;  /* Echo Request */
        icmp[1] = 0;  /* Code */
        icmp[4] = (uint8_t)(_ping_seq >> 8);
        icmp[5] = (uint8_t)(_ping_seq & 0xFF);

        /* Checksum */
        uint32_t sum = 0;
        for (int j = 0; j < 64; j += 2)
            sum += ((uint32_t)icmp[j] << 8) | icmp[j + 1];
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        uint16_t cksum = (uint16_t)~sum;
        icmp[2] = (uint8_t)(cksum >> 8);
        icmp[3] = (uint8_t)(cksum & 0xFF);

        raw_sendto(pcb, pb, &dest);
        pbuf_free(pb);

        absolute_time_t start = get_absolute_time();
        absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
        while (!_ping_reply) {
            if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) break;
            net_poll();
            sleep_ms(5);
        }

        if (_ping_reply) {
            int ms = (int)(absolute_time_diff_us(start, get_absolute_time()) / 1000);
            _ping_time_ms = ms;
            ok_count++;
        }

        if (i < count - 1) sleep_ms(500);
    }

    raw_remove(pcb);
    return ok_count;
}

/* ============================================================
 * NTP time sync
 * ============================================================ */
#define NTP_PORT        123
#define NTP_PACKET_LEN  48
#define NTP_UNIX_OFFSET 2208988800ULL

static volatile bool _ntp_done;

static void _ntp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    (void)arg; (void)pcb; (void)addr; (void)port;
    if (p && p->tot_len >= NTP_PACKET_LEN) {
        uint8_t *d = (uint8_t *)p->payload;
        /* Transmit timestamp starts at byte 40 (4 bytes seconds) */
        uint32_t secs = ((uint32_t)d[40] << 24) | ((uint32_t)d[41] << 16) |
                        ((uint32_t)d[42] << 8) | d[43];
        if (secs > NTP_UNIX_OFFSET) {
            _ntp_epoch_sec = secs - NTP_UNIX_OFFSET;
            _ntp_sync_us = time_us_64();
            _ntp_valid = true;
        }
    }
    if (p) pbuf_free(p);
    _ntp_done = true;
}

int net_ntp_sync(const char *server, int timeout_ms) {
    if (!server) server = "pool.ntp.org";

    uint32_t ntp_ip;
    if (net_dns_resolve(server, &ntp_ip, 5000) < 0) return -1;

    struct udp_pcb *pcb = udp_new();
    if (!pcb) return -1;

    udp_recv(pcb, _ntp_recv_cb, NULL);

    /* Build NTP request */
    struct pbuf *pb = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_LEN, PBUF_RAM);
    if (!pb) { udp_remove(pcb); return -1; }
    uint8_t *ntp = (uint8_t *)pb->payload;
    memset(ntp, 0, NTP_PACKET_LEN);
    ntp[0] = 0x23; /* LI=0, VN=4, Mode=3 (client) */

    ip_addr_t dest;
    ip4_addr_set_u32(ip_2_ip4(&dest), ntp_ip);
    IP_SET_TYPE(&dest, IPADDR_TYPE_V4);

    _ntp_done = false;
    udp_sendto(pcb, pb, &dest, NTP_PORT);
    pbuf_free(pb);

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!_ntp_done) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) break;
        net_poll();
        sleep_ms(10);
    }

    udp_remove(pcb);
    return _ntp_valid ? 0 : -1;
}

bool net_ntp_valid(void) { return _ntp_valid; }

void net_ntp_time(int *hour, int *min, int *sec) {
    if (!_ntp_valid) { *hour = *min = *sec = 0; return; }
    uint64_t now = _ntp_epoch_sec + (time_us_64() - _ntp_sync_us) / 1000000ULL;
    uint32_t tod = (uint32_t)(now % 86400);
    *hour = (int)(tod / 3600);
    *min  = (int)((tod / 60) % 60);
    *sec  = (int)(tod % 60);
}

void net_ntp_date(int *year, int *month, int *day) {
    if (!_ntp_valid) { *year = 0; *month = 0; *day = 0; return; }
    uint64_t now = _ntp_epoch_sec + (time_us_64() - _ntp_sync_us) / 1000000ULL;
    /* Simplified date calculation from Unix epoch */
    int days = (int)(now / 86400);
    int y = 1970;
    while (1) {
        int ydays = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366 : 365;
        if (days < ydays) break;
        days -= ydays;
        y++;
    }
    *year = y;
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 1 : 0;
    int m = 0;
    while (m < 12) {
        int md = mdays[m] + (m == 1 ? leap : 0);
        if (days < md) break;
        days -= md;
        m++;
    }
    *month = m + 1;
    *day = days + 1;
}

/* ============================================================
 * TCP client
 * ============================================================ */
struct net_tcp {
    struct tcp_pcb *pcb;
    volatile bool   connected;
    volatile bool   error;
    volatile bool   closed;
    uint8_t        *rxbuf;
    volatile int    rxlen;
    int             rxcap;
};

/* Statically allocate a small pool of connections */
#define NET_TCP_POOL 4
static struct net_tcp _tcp_pool[NET_TCP_POOL];
static uint8_t _tcp_rxbuf[NET_TCP_POOL][2048];

static err_t _tcp_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err) {
    (void)tpcb;
    net_tcp_t *c = (net_tcp_t *)arg;
    if (err == ERR_OK) c->connected = true;
    else c->error = true;
    return ERR_OK;
}

static err_t _tcp_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    net_tcp_t *c = (net_tcp_t *)arg;
    if (!p || err != ERR_OK) {
        c->closed = true;
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    int avail = c->rxcap - c->rxlen;
    int copy = (int)p->tot_len;
    if (copy > avail) copy = avail;
    if (copy > 0) {
        pbuf_copy_partial(p, c->rxbuf + c->rxlen, (u16_t)copy, 0);
        c->rxlen += copy;
    }
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void _tcp_err_cb(void *arg, err_t err) {
    (void)err;
    net_tcp_t *c = (net_tcp_t *)arg;
    c->error = true;
    c->pcb = NULL; /* pcb already freed by lwIP on error */
}

net_tcp_t *net_tcp_connect(uint32_t ip, uint16_t port, int timeout_ms) {
    /* Find a free slot */
    net_tcp_t *c = NULL;
    for (int i = 0; i < NET_TCP_POOL; i++) {
        if (!_tcp_pool[i].pcb && !_tcp_pool[i].connected) {
            c = &_tcp_pool[i];
            c->rxbuf = _tcp_rxbuf[i];
            c->rxcap = (int)sizeof(_tcp_rxbuf[i]);
            break;
        }
    }
    if (!c) return NULL;

    memset(c, 0, sizeof(*c));
    c->rxbuf = _tcp_rxbuf[c - _tcp_pool];
    c->rxcap = (int)sizeof(_tcp_rxbuf[0]);

    struct tcp_pcb *pcb = tcp_new();
    if (!pcb) return NULL;

    c->pcb = pcb;
    tcp_arg(pcb, c);
    tcp_err(pcb, _tcp_err_cb);
    tcp_recv(pcb, _tcp_recv_cb);

    ip_addr_t dest;
    ip4_addr_set_u32(ip_2_ip4(&dest), ip);
    IP_SET_TYPE(&dest, IPADDR_TYPE_V4);

    err_t err = tcp_connect(pcb, &dest, port, _tcp_connected_cb);
    if (err != ERR_OK) {
        tcp_abort(pcb);
        c->pcb = NULL;
        return NULL;
    }

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!c->connected && !c->error) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) {
            tcp_abort(pcb);
            c->pcb = NULL;
            return NULL;
        }
        net_poll();
        sleep_ms(10);
    }

    if (c->error || !c->connected) {
        if (c->pcb) { tcp_abort(c->pcb); c->pcb = NULL; }
        return NULL;
    }
    return c;
}

int net_tcp_send(net_tcp_t *conn, const void *data, size_t len) {
    if (!conn || !conn->pcb || conn->error) return -1;
    err_t err = tcp_write(conn->pcb, data, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) return -1;
    tcp_output(conn->pcb);
    return (int)len;
}

int net_tcp_recv(net_tcp_t *conn, void *buf, size_t cap, int timeout_ms) {
    if (!conn) return -1;

    /* Reset receive buffer */
    conn->rxlen = 0;

    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (conn->rxlen == 0 && !conn->closed && !conn->error) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) < 0) break;
        net_poll();
        sleep_ms(5);
    }

    int n = conn->rxlen;
    if (n > (int)cap) n = (int)cap;
    if (n > 0) memcpy(buf, conn->rxbuf, (size_t)n);
    return n;
}

void net_tcp_close(net_tcp_t *conn) {
    if (!conn) return;
    if (conn->pcb) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_close(conn->pcb);
        conn->pcb = NULL;
    }
    conn->connected = false;
    conn->closed = false;
    conn->error = false;
    conn->rxlen = 0;
}

/* ============================================================
 * HTTP GET (simple — no TLS)
 * ============================================================ */
int net_http_get(const char *host, uint16_t port, const char *path,
                 char *buf, size_t cap, int timeout_ms) {
    if (!host || !path || !buf || cap == 0) return -1;

    uint32_t ip;
    if (net_dns_resolve(host, &ip, 5000) < 0) return -1;

    net_tcp_t *c = net_tcp_connect(ip, port, timeout_ms);
    if (!c) return -1;

    /* Send HTTP request */
    char req[512];
    int rlen = snprintf(req, sizeof req,
        "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n"
        "User-Agent: MellivoraOS/1.0\r\n\r\n", path, host);
    if (rlen <= 0 || net_tcp_send(c, req, (size_t)rlen) < 0) {
        net_tcp_close(c);
        return -1;
    }

    /* Receive response — accumulate all data */
    int total = 0;
    while (total < (int)cap - 1) {
        char tmp[1024];
        int n = net_tcp_recv(c, tmp, sizeof tmp, timeout_ms);
        if (n <= 0) break;
        int copy = n;
        if (total + copy >= (int)cap) copy = (int)cap - 1 - total;
        memcpy(buf + total, tmp, (size_t)copy);
        total += copy;
    }
    buf[total] = '\0';
    net_tcp_close(c);

    /* Skip HTTP headers — find \r\n\r\n */
    char *body = strstr(buf, "\r\n\r\n");
    if (body) {
        body += 4;
        int blen = total - (int)(body - buf);
        memmove(buf, body, (size_t)blen);
        buf[blen] = '\0';
        return blen;
    }
    return total;
}

#endif /* PICO_CYW43_SUPPORTED */
