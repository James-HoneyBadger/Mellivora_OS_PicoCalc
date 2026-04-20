/*
 * netapps.c — Network utilities and programs for Mellivora OS PicoCalc
 *
 * Only compiled when PICO_CYW43_SUPPORTED is defined (Pico 2W).
 * All commands are prefixed net_app_ and registered in the main dispatch.
 */

#ifdef PICO_CYW43_SUPPORTED

#include "net.h"
#include "syscall.h"
#include "lcd.h"
#include "kbd.h"

#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

/* Saved WiFi credentials for reconnect */
static char _wifi_ssid[33]  = "";
static char _wifi_pass[65]  = "";

/* ============================================================
 * Helper: read a line from keyboard (blocking, with echo)
 * ============================================================ */
static int _net_readline(const char *prompt, char *buf, int cap, bool hide) {
    sys_print(prompt);
    int idx = 0;
    while (idx < cap - 1) {
        int ch = kbd_getc();
        if (ch < 0) { sleep_ms(20); continue; }
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8 || ch == 127) {
            if (idx > 0) { idx--; sys_putchar(8); sys_putchar(' '); sys_putchar(8); }
            continue;
        }
        if (ch == 0x03) return -1; /* Ctrl+C */
        buf[idx++] = (char)ch;
        sys_putchar(hide ? '*' : (char)ch);
    }
    buf[idx] = '\0';
    sys_putchar('\n');
    return idx;
}

/* ============================================================
 * wifi — connect, disconnect, scan, status
 * ============================================================ */
static void _scan_print_cb(const char *ssid, int rssi, int auth, void *ctx) {
    (void)ctx;
    const char *astr = "OPEN";
    if (auth == 2) astr = "WPA";
    if (auth == 3) astr = "WPA2";
    char line[64];
    snprintf(line, sizeof line, "  %-24s %4d dBm  %s", ssid, rssi, astr);
    sys_print(line);
    sys_putchar('\n');
}

void net_app_wifi(const char *arg) {
    if (!arg || !*arg) {
        /* Show status */
        if (net_is_connected()) {
            net_ifinfo_t info;
            net_get_ifinfo(&info);
            char ip[16];
            net_ip_to_str(info.ip, ip, sizeof ip);
            char line[80];
            snprintf(line, sizeof line, "Connected to: %s  IP: %s", _wifi_ssid, ip);
            sys_print(line);
            sys_putchar('\n');
        } else {
            sys_print("WiFi: not connected\n");
        }
        sys_print("Usage: wifi connect|disconnect|scan|status\n");
        return;
    }

    if (!strcmp(arg, "scan")) {
        sys_print("Scanning WiFi networks...\n");
        if (net_init() < 0) { sys_print("WiFi init failed\n"); return; }
        net_wifi_scan(_scan_print_cb, NULL);
        return;
    }

    if (!strcmp(arg, "disconnect")) {
        net_wifi_disconnect();
        sys_print("WiFi disconnected\n");
        return;
    }

    if (!strcmp(arg, "status")) {
        if (net_is_connected()) {
            net_ifinfo_t info;
            net_get_ifinfo(&info);
            char ip[16], gw[16], nm[16], dns[16];
            net_ip_to_str(info.ip, ip, sizeof ip);
            net_ip_to_str(info.gateway, gw, sizeof gw);
            net_ip_to_str(info.netmask, nm, sizeof nm);
            net_ip_to_str(info.dns, dns, sizeof dns);
            char line[80];
            snprintf(line, sizeof line, "SSID:    %s", _wifi_ssid);
            sys_print(line); sys_putchar('\n');
            snprintf(line, sizeof line, "IP:      %s", ip);
            sys_print(line); sys_putchar('\n');
            snprintf(line, sizeof line, "Netmask: %s", nm);
            sys_print(line); sys_putchar('\n');
            snprintf(line, sizeof line, "Gateway: %s", gw);
            sys_print(line); sys_putchar('\n');
            snprintf(line, sizeof line, "DNS:     %s", dns);
            sys_print(line); sys_putchar('\n');
        } else {
            sys_print("WiFi: not connected\n");
        }
        return;
    }

    if (!strncmp(arg, "connect", 7)) {
        const char *rest = arg + 7;
        while (*rest == ' ') rest++;

        char ssid[33] = "", pass[65] = "";
        if (*rest) {
            /* wifi connect SSID [PASSWORD] */
            const char *sp = strchr(rest, ' ');
            if (sp) {
                size_t slen = (size_t)(sp - rest);
                if (slen > 32) slen = 32;
                memcpy(ssid, rest, slen);
                ssid[slen] = '\0';
                sp++;
                while (*sp == ' ') sp++;
                strncpy(pass, sp, 64);
                pass[64] = '\0';
            } else {
                strncpy(ssid, rest, 32);
                ssid[32] = '\0';
            }
        }

        if (!ssid[0]) {
            if (_net_readline("SSID: ", ssid, 33, false) < 0) return;
        }
        if (!pass[0]) {
            _net_readline("Password: ", pass, 65, true);
        }

        sys_print("Connecting to ");
        sys_print(ssid);
        sys_print("...\n");

        if (net_init() < 0) { sys_print("WiFi init failed\n"); return; }
        if (net_wifi_connect(ssid, pass[0] ? pass : NULL, 15000) == 0) {
            strncpy(_wifi_ssid, ssid, sizeof _wifi_ssid - 1);
            strncpy(_wifi_pass, pass, sizeof _wifi_pass - 1);
            net_ifinfo_t info;
            net_get_ifinfo(&info);
            char ip[16];
            net_ip_to_str(info.ip, ip, sizeof ip);
            sys_print("Connected! IP: ");
            sys_print(ip);
            sys_putchar('\n');
        } else {
            sys_print("Connection failed\n");
        }
        return;
    }

    sys_print("wifi: unknown subcommand\n");
    sys_print("Usage: wifi connect|disconnect|scan|status\n");
}

/* ============================================================
 * ping — ICMP echo
 * ============================================================ */
void net_app_ping(const char *arg) {
    if (!arg || !*arg) {
        sys_print("Usage: ping <host> [count]\n");
        return;
    }
    if (!net_is_connected()) {
        sys_print("ping: not connected to WiFi\n");
        return;
    }

    char host[128] = "";
    int count = 4;
    const char *sp = strchr(arg, ' ');
    if (sp) {
        size_t hlen = (size_t)(sp - arg);
        if (hlen > 127) hlen = 127;
        memcpy(host, arg, hlen);
        host[hlen] = '\0';
        count = atoi(sp + 1);
        if (count < 1) count = 1;
        if (count > 20) count = 20;
    } else {
        strncpy(host, arg, 127);
        host[127] = '\0';
    }

    sys_print("Resolving ");
    sys_print(host);
    sys_print("...\n");

    uint32_t ip;
    if (net_dns_resolve(host, &ip, 5000) < 0) {
        sys_print("ping: DNS resolution failed\n");
        return;
    }

    char ipstr[16];
    net_ip_to_str(ip, ipstr, sizeof ipstr);
    char line[80];
    snprintf(line, sizeof line, "PING %s (%s): %d packets", host, ipstr, count);
    sys_print(line);
    sys_putchar('\n');

    int ok = net_ping(ip, count, 3000);
    snprintf(line, sizeof line, "%d/%d packets received", ok, count);
    sys_print(line);
    sys_putchar('\n');
}

/* ============================================================
 * ifconfig — show network interface info
 * ============================================================ */
void net_app_ifconfig(const char *arg) {
    (void)arg;
    if (!net_is_connected()) {
        sys_print("wlan0: DOWN\n");
        return;
    }
    net_ifinfo_t info;
    net_get_ifinfo(&info);
    char ip[16], gw[16], nm[16], dns[16];
    net_ip_to_str(info.ip, ip, sizeof ip);
    net_ip_to_str(info.gateway, gw, sizeof gw);
    net_ip_to_str(info.netmask, nm, sizeof nm);
    net_ip_to_str(info.dns, dns, sizeof dns);

    char line[80];
    snprintf(line, sizeof line, "wlan0: UP  SSID: %s", _wifi_ssid);
    sys_print(line); sys_putchar('\n');
    snprintf(line, sizeof line, "  inet %s  netmask %s", ip, nm);
    sys_print(line); sys_putchar('\n');
    snprintf(line, sizeof line, "  gateway %s  dns %s", gw, dns);
    sys_print(line); sys_putchar('\n');
}

/* ============================================================
 * ntp — synchronize time from NTP server
 * ============================================================ */
void net_app_ntp(const char *arg) {
    if (!net_is_connected()) {
        sys_print("ntp: not connected to WiFi\n");
        return;
    }

    const char *server = (arg && *arg) ? arg : "pool.ntp.org";
    sys_print("Syncing time from ");
    sys_print(server);
    sys_print("...\n");

    if (net_ntp_sync(server, 5000) == 0) {
        int h, m, s, yr, mo, dy;
        net_ntp_time(&h, &m, &s);
        net_ntp_date(&yr, &mo, &dy);
        char line[64];
        snprintf(line, sizeof line, "Time: %04d-%02d-%02d %02d:%02d:%02d UTC",
                 yr, mo, dy, h, m, s);
        sys_print(line);
        sys_putchar('\n');
    } else {
        sys_print("NTP sync failed\n");
    }
}

/* ============================================================
 * dns — resolve a hostname
 * ============================================================ */
void net_app_dns(const char *arg) {
    if (!arg || !*arg) {
        sys_print("Usage: dns <hostname>\n");
        return;
    }
    if (!net_is_connected()) {
        sys_print("dns: not connected to WiFi\n");
        return;
    }

    uint32_t ip;
    if (net_dns_resolve(arg, &ip, 5000) == 0) {
        char ipstr[16];
        net_ip_to_str(ip, ipstr, sizeof ipstr);
        sys_print(arg);
        sys_print(" -> ");
        sys_print(ipstr);
        sys_putchar('\n');
    } else {
        sys_print("dns: resolution failed\n");
    }
}

/* ============================================================
 * fetch — HTTP GET and display body
 * ============================================================ */
void net_app_fetch(const char *arg) {
    if (!arg || !*arg) {
        sys_print("Usage: fetch <url>\n");
        sys_print("  e.g. fetch http://example.com/\n");
        sys_print("  e.g. fetch example.com /path\n");
        return;
    }
    if (!net_is_connected()) {
        sys_print("fetch: not connected to WiFi\n");
        return;
    }

    char host[128] = "";
    char path[256] = "/";
    uint16_t port = 80;

    /* Parse URL */
    const char *p = arg;
    if (!strncmp(p, "http://", 7)) p += 7;

    /* Extract host */
    const char *slash = strchr(p, '/');
    const char *space = strchr(p, ' ');

    if (slash && (!space || slash < space)) {
        size_t hlen = (size_t)(slash - p);
        if (hlen > 127) hlen = 127;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, 255);
        path[255] = '\0';
    } else if (space) {
        size_t hlen = (size_t)(space - p);
        if (hlen > 127) hlen = 127;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        while (*space == ' ') space++;
        if (*space) { strncpy(path, space, 255); path[255] = '\0'; }
    } else {
        strncpy(host, p, 127);
        host[127] = '\0';
    }

    /* Check for port in host */
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 80;
    }

    char line[160];
    snprintf(line, sizeof line, "GET %s:%d%s ...", host, port, path);
    sys_print(line);
    sys_putchar('\n');

    static char response[8192];
    int n = net_http_get(host, port, path, response, sizeof response, 10000);
    if (n < 0) {
        sys_print("fetch: request failed\n");
        return;
    }

    /* Display response body */
    for (int i = 0; i < n; i++) {
        char c = response[i];
        if (c == '\n' || (c >= ' ' && c < 127) || c == '\t')
            sys_putchar(c);
    }
    sys_putchar('\n');

    snprintf(line, sizeof line, "(%d bytes received)", n);
    sys_print(line);
    sys_putchar('\n');
}

/* ============================================================
 * wget — download a URL to a file on the SD card
 * ============================================================ */
void net_app_wget(const char *arg) {
    if (!arg || !*arg) {
        sys_print("Usage: wget <url> <file>\n");
        sys_print("  e.g. wget http://example.com/ /INDEX.HTM\n");
        return;
    }
    if (!net_is_connected()) {
        sys_print("wget: not connected to WiFi\n");
        return;
    }

    /* Split into URL and filename */
    char url_part[256] = "", file_part[128] = "";
    const char *sp = NULL;

    /* Find last space — everything after is the filename */
    for (const char *s = arg + strlen(arg) - 1; s > arg; s--) {
        if (*s == ' ') { sp = s; break; }
    }

    if (!sp) {
        sys_print("wget: missing output file\n");
        sys_print("Usage: wget <url> <file>\n");
        return;
    }

    size_t ulen = (size_t)(sp - arg);
    if (ulen > 255) ulen = 255;
    memcpy(url_part, arg, ulen);
    url_part[ulen] = '\0';
    sp++;
    while (*sp == ' ') sp++;
    strncpy(file_part, sp, 127);
    file_part[127] = '\0';

    /* Parse URL */
    char host[128] = "";
    char path[256] = "/";
    uint16_t port = 80;

    const char *p = url_part;
    if (!strncmp(p, "http://", 7)) p += 7;

    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen > 127) hlen = 127;
        memcpy(host, p, hlen);
        host[hlen] = '\0';
        strncpy(path, slash, 255);
        path[255] = '\0';
    } else {
        strncpy(host, p, 127);
        host[127] = '\0';
    }

    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = (uint16_t)atoi(colon + 1);
        if (port == 0) port = 80;
    }

    sys_print("Downloading ");
    sys_print(host);
    sys_print(path);
    sys_print(" -> ");
    sys_print(file_part);
    sys_putchar('\n');

    static char response[8192];
    int n = net_http_get(host, port, path, response, sizeof response, 15000);
    if (n <= 0) {
        sys_print("wget: download failed\n");
        return;
    }

    int rc = sys_fwrite(file_part, response, n);
    if (rc < 0) {
        sys_print("wget: file write failed\n");
        return;
    }

    char line[64];
    snprintf(line, sizeof line, "Saved %d bytes to %s", n, file_part);
    sys_print(line);
    sys_putchar('\n');
}

/* ============================================================
 * weather — fetch weather from wttr.in
 * ============================================================ */
void net_app_weather(const char *arg) {
    if (!net_is_connected()) {
        sys_print("weather: not connected to WiFi\n");
        return;
    }

    char path[128] = "/";
    if (arg && *arg) {
        snprintf(path, sizeof path, "/%s?format=3", arg);
    } else {
        strncpy(path, "/?format=3", sizeof path - 1);
    }

    sys_print("Fetching weather...\n");

    static char response[2048];
    int n = net_http_get("wttr.in", 80, path, response, sizeof response, 10000);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            char c = response[i];
            if (c == '\n' || (c >= ' ' && c < 127)) sys_putchar(c);
        }
        sys_putchar('\n');
    } else {
        sys_print("weather: request failed\n");
    }
}

/* ============================================================
 * irc — minimal IRC client
 * ============================================================ */
void net_app_irc(const char *arg) {
    if (!net_is_connected()) {
        sys_print("irc: not connected to WiFi\n");
        return;
    }

    char server[64] = "irc.libera.chat";
    uint16_t port = 6667;
    char nick[16] = "mellivora";
    char channel[32] = "";

    /* Parse: irc [server [#channel [nick]]] */
    if (arg && *arg) {
        char buf[128];
        strncpy(buf, arg, 127);
        buf[127] = '\0';
        char *tok = strtok(buf, " ");
        if (tok) { strncpy(server, tok, 63); server[63] = '\0'; }
        tok = strtok(NULL, " ");
        if (tok) { strncpy(channel, tok, 31); channel[31] = '\0'; }
        tok = strtok(NULL, " ");
        if (tok) { strncpy(nick, tok, 15); nick[15] = '\0'; }
    }

    sys_print("Connecting to ");
    sys_print(server);
    sys_print("...\n");

    uint32_t ip;
    if (net_dns_resolve(server, &ip, 5000) < 0) {
        sys_print("irc: DNS failed\n");
        return;
    }

    net_tcp_t *conn = net_tcp_connect(ip, port, 10000);
    if (!conn) {
        sys_print("irc: connection failed\n");
        return;
    }

    /* Send NICK and USER */
    char cmd[128];
    snprintf(cmd, sizeof cmd, "NICK %s\r\n", nick);
    net_tcp_send(conn, cmd, strlen(cmd));
    snprintf(cmd, sizeof cmd, "USER %s 0 * :Mellivora IRC\r\n", nick);
    net_tcp_send(conn, cmd, strlen(cmd));

    /* Join channel if specified */
    if (channel[0]) {
        sleep_ms(1000);
        net_poll();
        snprintf(cmd, sizeof cmd, "JOIN %s\r\n", channel);
        net_tcp_send(conn, cmd, strlen(cmd));
    }

    sys_print("Connected. Type /quit to exit, /join #channel, /msg nick text\n");

    char line[256];
    int lidx = 0;

    for (;;) {
        /* Check for incoming data */
        char rxbuf[512];
        int n = net_tcp_recv(conn, rxbuf, sizeof rxbuf - 1, 100);
        if (n > 0) {
            rxbuf[n] = '\0';

            /* Handle PING */
            if (!strncmp(rxbuf, "PING ", 5)) {
                char pong[128];
                snprintf(pong, sizeof pong, "PONG %s", rxbuf + 5);
                net_tcp_send(conn, pong, strlen(pong));
            }

            /* Display received lines (strip formatting) */
            for (int i = 0; i < n; i++) {
                char c = rxbuf[i];
                if (c == '\n') { sys_putchar('\n'); }
                else if (c >= ' ' && c < 127) sys_putchar(c);
            }
        }

        /* Check for keyboard input */
        int ch = kbd_getc();
        if (ch < 0) { net_poll(); continue; }

        if (ch == '\r' || ch == '\n') {
            sys_putchar('\n');
            line[lidx] = '\0';

            if (!strcmp(line, "/quit")) {
                net_tcp_send(conn, "QUIT :bye\r\n", 11);
                break;
            }
            if (!strncmp(line, "/join ", 6)) {
                snprintf(cmd, sizeof cmd, "JOIN %s\r\n", line + 6);
                net_tcp_send(conn, cmd, strlen(cmd));
                strncpy(channel, line + 6, 31);
            } else if (!strncmp(line, "/msg ", 5)) {
                snprintf(cmd, sizeof cmd, "PRIVMSG %s\r\n", line + 5);
                net_tcp_send(conn, cmd, strlen(cmd));
            } else if (line[0] == '/') {
                /* Raw IRC command */
                snprintf(cmd, sizeof cmd, "%s\r\n", line + 1);
                net_tcp_send(conn, cmd, strlen(cmd));
            } else if (channel[0] && lidx > 0) {
                /* Send to current channel */
                snprintf(cmd, sizeof cmd, "PRIVMSG %s :%s\r\n", channel, line);
                net_tcp_send(conn, cmd, strlen(cmd));
            }
            lidx = 0;
        } else if (ch == 8 || ch == 127) {
            if (lidx > 0) { lidx--; sys_putchar(8); sys_putchar(' '); sys_putchar(8); }
        } else if (ch == 0x03) { /* Ctrl+C */
            net_tcp_send(conn, "QUIT :bye\r\n", 11);
            break;
        } else if (lidx < 255) {
            line[lidx++] = (char)ch;
            sys_putchar((char)ch);
        }
    }

    net_tcp_close(conn);
    sys_print("IRC disconnected\n");
}

/* ============================================================
 * netstat — show connection pool status
 * ============================================================ */
void net_app_netstat(const char *arg) {
    (void)arg;
    sys_print("Network status:\n");
    if (net_is_connected()) {
        net_ifinfo_t info;
        net_get_ifinfo(&info);
        char ip[16];
        net_ip_to_str(info.ip, ip, sizeof ip);
        char line[64];
        snprintf(line, sizeof line, "  wlan0: UP  %s  SSID: %s", ip, _wifi_ssid);
        sys_print(line);
        sys_putchar('\n');
    } else {
        sys_print("  wlan0: DOWN\n");
    }
    if (net_ntp_valid()) {
        int h, m, s;
        net_ntp_time(&h, &m, &s);
        char line[32];
        snprintf(line, sizeof line, "  NTP:   %02d:%02d:%02d UTC", h, m, s);
        sys_print(line);
        sys_putchar('\n');
    } else {
        sys_print("  NTP:   not synced\n");
    }
}

/* ============================================================
 * telnet — minimal telnet client
 * ============================================================ */
void net_app_telnet(const char *arg) {
    if (!arg || !*arg) {
        sys_print("Usage: telnet <host> [port]\n");
        return;
    }
    if (!net_is_connected()) {
        sys_print("telnet: not connected to WiFi\n");
        return;
    }

    char host[128] = "";
    uint16_t port = 23;
    const char *sp = strchr(arg, ' ');
    if (sp) {
        size_t hlen = (size_t)(sp - arg);
        if (hlen > 127) hlen = 127;
        memcpy(host, arg, hlen);
        host[hlen] = '\0';
        port = (uint16_t)atoi(sp + 1);
        if (port == 0) port = 23;
    } else {
        strncpy(host, arg, 127);
        host[127] = '\0';
    }

    sys_print("Connecting to ");
    sys_print(host);
    sys_print("...\n");

    uint32_t ip;
    if (net_dns_resolve(host, &ip, 5000) < 0) {
        sys_print("telnet: DNS failed\n");
        return;
    }

    net_tcp_t *conn = net_tcp_connect(ip, port, 10000);
    if (!conn) {
        sys_print("telnet: connection failed\n");
        return;
    }

    sys_print("Connected. Ctrl+C to disconnect.\n");

    for (;;) {
        /* Receive */
        char rxbuf[512];
        int n = net_tcp_recv(conn, rxbuf, sizeof rxbuf - 1, 100);
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                char c = rxbuf[i];
                /* Skip telnet negotiation bytes (IAC = 0xFF) */
                if ((uint8_t)c == 0xFF) { i += 2; continue; }
                if (c == '\n' || (c >= ' ' && c < 127) || c == '\t' || c == '\r')
                    sys_putchar(c);
            }
        }

        /* Send keyboard input */
        int ch = kbd_getc();
        if (ch < 0) { net_poll(); continue; }
        if (ch == 0x03) break; /* Ctrl+C */
        char c = (char)ch;
        if (ch == '\r') {
            net_tcp_send(conn, "\r\n", 2);
            sys_putchar('\n');
        } else {
            net_tcp_send(conn, &c, 1);
            sys_putchar(c);
        }
    }

    net_tcp_close(conn);
    sys_print("\nConnection closed\n");
}

#endif /* PICO_CYW43_SUPPORTED */
