/*
 * netapps.h — Network application commands for Mellivora OS PicoCalc
 *
 * Only available when PICO_CYW43_SUPPORTED is defined (Pico 2W).
 */

#pragma once

#ifdef PICO_CYW43_SUPPORTED

void net_app_wifi(const char *arg);
void net_app_ping(const char *arg);
void net_app_ifconfig(const char *arg);
void net_app_ntp(const char *arg);
void net_app_dns(const char *arg);
void net_app_fetch(const char *arg);
void net_app_wget(const char *arg);
void net_app_weather(const char *arg);
void net_app_irc(const char *arg);
void net_app_netstat(const char *arg);
void net_app_telnet(const char *arg);

/* Try to connect to any saved WiFi network. Returns 0 on success.
 * Loads /WIFI.CFG on first call. Safe to call from boot. */
int  net_app_wifi_autoconnect(void);

#endif /* PICO_CYW43_SUPPORTED */
