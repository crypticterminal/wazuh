/*
 * Wazuh Module for System inventory for Linux
 * Copyright (C) 2017 Wazuh Inc.
 * 23 Aug, 2017.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#ifndef WIN32

#include "syscollector.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <linux/if_link.h>
#include <linux/if_packet.h>

char* check_dhcp(char *ifa_name, int family);   // Check DHCP status for network interfaces
char* get_default_gateway(char *ifa_name);

// Get network inventory

void sys_network_linux(int queue_fd, const char* LOCATION){

    char ** ifaces_list;
    int i = 0, j = 0, k = 0, found;
    int family;
    struct ifaddrs *ifaddr, *ifa;

    mtinfo(WM_SYS_LOGTAG, "Starting network inventory.");

    if (getifaddrs(&ifaddr) == -1) {
        mterror(WM_SYS_LOGTAG, "getifaddrs() failed.");
        return;
    }

    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next){
        i++;
    }
    os_calloc(i, sizeof(char *), ifaces_list);

    /* Create interfaces list */
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next){
        found = 0;
        for (i=0; i<=j; i++){
            if (!ifaces_list[i]){
                if (!strcmp(ifa->ifa_name, "lo"))
                    found = 1;

                break;

            }else if (!strcmp(ifaces_list[i], ifa->ifa_name)){
                    found = 1;
                    break;
            }
        }
        if (!found){

            ifaces_list[j] = strdup(ifa->ifa_name);
            j++;
        }
    }

    if(!ifaces_list[j-1]){
        mterror(WM_SYS_LOGTAG, "Not found any interface. Network inventory suspended.");
        return;
    }

    /* Collect all information for each interface */
    for (i=0; i<j; i++){

        char *string;

        cJSON *object = cJSON_CreateObject();
        cJSON *interface = cJSON_CreateObject();
        cJSON_AddStringToObject(object, "type", "network");
        cJSON_AddItemToObject(object, "iface", interface);

        cJSON_AddStringToObject(interface, "name", ifaces_list[i]);

        for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {

            if (strcmp(ifaces_list[i], ifa->ifa_name)){
                continue;
            }
            if (!ifa->ifa_addr || !strcmp(ifa->ifa_name, "lo")) {
                continue;
            }

            family = ifa->ifa_addr->sa_family;

            if (family == AF_INET) {

                cJSON *ipv4 = cJSON_CreateObject();
                cJSON_AddItemToObject(interface, "IPv4", ipv4);

                /* Get IPv4 address */
                char host[NI_MAXHOST];
                int result = getnameinfo(ifa->ifa_addr,
                        sizeof(struct sockaddr_in),
                        host, NI_MAXHOST,
                        NULL, 0, NI_NUMERICHOST);
                if (result == 0) {
                    cJSON_AddStringToObject(ipv4, "address", host);
                } else {
                    mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                }

                /* Get Netmask for IPv4 address */
                char netmask[NI_MAXHOST];
                result = getnameinfo(ifa->ifa_netmask,
                    sizeof(struct sockaddr_in),
                    netmask, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);

                if (result == 0) {
                    cJSON_AddStringToObject(ipv4, "netmask", netmask);
                } else {
                    mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                }

                /* Get broadcast address (or destination address in a Point to Point connection) */
                if (ifa->ifa_ifu.ifu_broadaddr != NULL){
                    if (ifa->ifa_flags & IFF_POINTOPOINT){
                        char dstaddr[NI_MAXHOST];
                        result = getnameinfo(ifa->ifa_ifu.ifu_dstaddr,
                            sizeof(struct sockaddr_in),
                            dstaddr, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);

                        if (result == 0) {
                            cJSON_AddStringToObject(ipv4, "dst_address", dstaddr);
                        } else {
                            mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                        }
                    }else{
                        char broadaddr[NI_MAXHOST];
                        result = getnameinfo(ifa->ifa_ifu.ifu_broadaddr,
                            sizeof(struct sockaddr_in),
                            broadaddr, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);

                        if (result == 0) {
                            cJSON_AddStringToObject(ipv4, "broadcast", broadaddr);
                        } else {
                            mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                        }
                    }
                }

                /* Get Default Gateway */
                char *gateway;
                gateway = get_default_gateway(ifaces_list[i]);
                cJSON_AddStringToObject(ipv4, "gateway", gateway);
                free(gateway);

                /* Get DHCP status for IPv4 */
                char *dhcp_status;
                dhcp_status = check_dhcp(ifaces_list[i], family);
                cJSON_AddStringToObject(ipv4, "DHCP", dhcp_status);
                free(dhcp_status);

            } else if (family == AF_INET6) {

                cJSON *ipv6 = cJSON_CreateObject();
                cJSON_AddItemToObject(interface, "IPv6", ipv6);

                /* Get IPv6 address */
                char host[NI_MAXHOST];
                int result = getnameinfo(ifa->ifa_addr,
                        sizeof(struct sockaddr_in6),
                        host, NI_MAXHOST,
                        NULL, 0, NI_NUMERICHOST);
                if (result == 0) {
                    char ** parts = NULL;
                    char *ip_addrr;
                    parts = OS_StrBreak('%', host, 2);
                    ip_addrr = w_strtrim(parts[0]);
                    cJSON_AddStringToObject(ipv6, "address", ip_addrr);
                    for (k=0; parts[k]; k++){
                        free(parts[k]);
                    }
                    free(parts);
                } else {
                    mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                }

                /* Get Netmask for IPv6 address */
                char netmask6[NI_MAXHOST];
                result = getnameinfo(ifa->ifa_netmask,
                    sizeof(struct sockaddr_in6),
                    netmask6, NI_MAXHOST,
                    NULL, 0, NI_NUMERICHOST);

                if (result == 0) {
                    cJSON_AddStringToObject(ipv6, "netmask", netmask6);
                } else {
                    mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                }

                /* Get broadcast address (or destination address in a Point to Point connection) for IPv6*/
                if (ifa->ifa_ifu.ifu_broadaddr != NULL){
                    if (ifa->ifa_flags & IFF_POINTOPOINT){
                        char dstaddr6[NI_MAXHOST];
                        result = getnameinfo(ifa->ifa_ifu.ifu_dstaddr,
                            sizeof(struct sockaddr_in6),
                            dstaddr6, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);

                        if (result == 0) {
                            cJSON_AddStringToObject(ipv6, "dst_address", dstaddr6);
                        } else {
                            mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                        }
                    }else{
                        char broadaddr6[NI_MAXHOST];
                        result = getnameinfo(ifa->ifa_ifu.ifu_broadaddr,
                            sizeof(struct sockaddr_in6),
                            broadaddr6, NI_MAXHOST,
                            NULL, 0, NI_NUMERICHOST);

                        if (result == 0) {
                            cJSON_AddStringToObject(ipv6, "broadcast", broadaddr6);
                        } else {
                            mterror(WM_SYS_LOGTAG, "getnameinfo() failed: %s\n", gai_strerror(result));
                        }
                    }
                }

                /* Get DHCP status for IPv6 */
                char *dhcp_status;
                dhcp_status = check_dhcp(ifaces_list[i], family);
                cJSON_AddStringToObject(ipv6, "DHCP", dhcp_status);
                free(dhcp_status);

            } else if (family == AF_PACKET && ifa->ifa_data != NULL){

                /* Get MAC address and stats */
                char MAC[18];
                struct rtnl_link_stats *stats = ifa->ifa_data;
                struct sockaddr_ll *addr = (struct sockaddr_ll*)ifa->ifa_addr;
                snprintf(MAC, 18, "%02X:%02X:%02X:%02X:%02X:%02X", addr->sll_addr[0], addr->sll_addr[1], addr->sll_addr[2], addr->sll_addr[3], addr->sll_addr[4], addr->sll_addr[5]);
                cJSON_AddStringToObject(interface, "MAC", MAC);
                cJSON_AddNumberToObject(interface, "tx_packets", stats->tx_packets);
                cJSON_AddNumberToObject(interface, "rx_packets", stats->rx_packets);
                cJSON_AddNumberToObject(interface, "tx_bytes", stats->tx_bytes);
                cJSON_AddNumberToObject(interface, "rx_bytes", stats->rx_bytes);

            }
        }

        /* Send interface data in JSON format */
        string = cJSON_PrintUnformatted(object);
        mtdebug2(WM_SYS_LOGTAG, "wm_sys_network() sending '%s'", string);
        SendMSG(queue_fd, string, LOCATION, WODLE_MQ);
        cJSON_Delete(object);

        free(string);
    }

    freeifaddrs(ifaddr);
    for (i=0; ifaces_list[i]; i++){
        free(ifaces_list[i]);
    }
    free(ifaces_list);

}

/* Check DHCP status for IPv4 and IPv6 addresses in each interface */
char* check_dhcp(char *ifa_name, int family){

    char file[256];
    char file_location[256];
    FILE *fp;
    DIR *dir;
    char string[OS_MAXSTR];
    char * iface_string;
    char * aux_string;
    int spaces;
    char * dhcp;
    os_calloc(DHCP_LENGTH + 1, sizeof(char), dhcp);

    snprintf(dhcp, DHCP_LENGTH, "%s", "unknown");
    snprintf(file_location, 256, "%s", WM_SYS_IF_FILE);

    /* Check DHCP configuration for Debian based systems */
    if ((fp=fopen(file_location, "r"))){

        while (fgets(string, OS_MAXSTR, fp) != NULL){

            if ((aux_string = strstr(string, "iface")) != NULL){

                spaces = strspn(aux_string, " \t");

                if ((iface_string = strstr(aux_string + 5 + spaces, ifa_name)) != NULL){

                    spaces = strspn(iface_string, " \t");
                    int ifa_length = strlen(ifa_name);

                    switch (family){

                        case AF_INET:
                            if ((aux_string = strstr(iface_string + ifa_length + spaces, "inet")) != NULL){

                                spaces = strspn(aux_string, " \t");
                                if (strstr(aux_string + 4 + spaces, "static") || strstr(aux_string + 4 + spaces, "manual")){
                                    snprintf(dhcp, DHCP_LENGTH, "%s", "disabled");
                                    fclose(fp);
                                    return dhcp;
                                }else if (strstr(aux_string + 4 + spaces, "dhcp")){
                                    snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                    fclose(fp);
                                    return dhcp;
                                }
                            }else{

                                snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                fclose(fp);
                                return dhcp;
                            }
                            break;

                        case AF_INET6:

                            if ((aux_string = strstr(iface_string + ifa_length + spaces, "inet6")) != NULL){

                                spaces = strspn(aux_string, " \t");
                                if (strstr(aux_string + 5 + spaces, "static") || strstr(aux_string + 5 + spaces, "manual")){
                                    snprintf(dhcp, DHCP_LENGTH, "%s", "disabled");
                                    fclose(fp);
                                    return dhcp;
                                }else if (strstr(aux_string + 5 + spaces, "dhcp")){
                                    snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                    fclose(fp);
                                    return dhcp;
                                }
                            }else{

                                snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                fclose(fp);
                                return dhcp;
                            }
                            break;

                        default:
                            mtwarn(WM_SYS_LOGTAG, "Unknown DHCP configuration.");
                            break;
                    }
                }

            }
        }
        snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
        fclose(fp);

    }else{

        /* Check DHCP configuration for Red Hat based systems and SUSE distributions */
        snprintf(file, 256, "%s%s", "ifcfg-", ifa_name);

        if ((dir=opendir(WM_SYS_IF_DIR_RH))){
            snprintf(file_location, 256, "%s%s", WM_SYS_IF_DIR_RH, file);
            snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
            closedir(dir);
        }

        /* For SUSE Linux distributions */
        if ((dir=opendir(WM_SYS_IF_DIR_SUSE))){
            snprintf(file_location, 256, "%s%s", WM_SYS_IF_DIR_SUSE, file);
            snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
            closedir(dir);
        }

        if ((fp=fopen(file_location, "r"))){

            switch (family){
                case AF_INET:

                    while (fgets(string, OS_MAXSTR, fp) != NULL){

                        if (strstr(string, "BOOTPROTO") != NULL){

                            if (strstr(string, "static") || strstr(string, "none")){
                                snprintf(dhcp, DHCP_LENGTH, "%s", "disabled");
                                fclose(fp);
                                return dhcp;
                            }else if (strstr(string, "bootp")){
                                snprintf(dhcp, DHCP_LENGTH, "%s", "BOOTP");
                                fclose(fp);
                                return dhcp;
                            }else if (strstr(string, "dhcp")){
                                snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                fclose(fp);
                                return dhcp;
                            }else{
                                snprintf(dhcp, DHCP_LENGTH, "%s", "unknown");
                                fclose(fp);
                                return dhcp;
                            }
                        }
                    }
                    break;

                case AF_INET6:

                    while (fgets(string, OS_MAXSTR, fp) != NULL){

                        if (strstr(string, "DHCPV6C") != NULL){
                            if (strstr(string, "no")){
                                snprintf(dhcp, DHCP_LENGTH, "%s", "disabled");
                                fclose(fp);
                                return dhcp;
                            }else if (strstr(string, "yes")){
                                snprintf(dhcp, DHCP_LENGTH, "%s", "enabled");
                                fclose(fp);
                                return dhcp;
                            }else {
                                snprintf(dhcp, DHCP_LENGTH, "%s", "unknown");
                                fclose(fp);
                                return dhcp;
                            }
                        }
                    }
                    break;

                default:
                    mtwarn(WM_SYS_LOGTAG, "Unknown DHCP configuration.");
                    break;
            }
            fclose(fp);
        }
    }

    return dhcp;
}

char* get_default_gateway(char *ifa_name){

    FILE *fp;
    char file_location[256];
    char interface[OS_MAXSTR];
    char string[OS_MAXSTR];
    in_addr_t address = 0;
    int destination, gateway;
    char * def_gateway;
    os_calloc(NI_MAXHOST, sizeof(char) + 1, def_gateway);

    strncpy(interface, ifa_name, strlen(ifa_name));
    snprintf(file_location, 256, "%s", WM_SYS_DGW_FILE);
    snprintf(def_gateway, NI_MAXHOST, "%s", "unknown");

    if ((fp=fopen(file_location, "r"))){

        while (fgets(string, OS_MAXSTR, fp) != NULL){

            if (sscanf(string, "%s %8x %8x", ifa_name, &destination, &gateway) == 3){
                if (destination == 00000000 && !strcmp(ifa_name, interface)){
                    address = gateway;
                    snprintf(def_gateway, NI_MAXHOST, "%s", inet_ntoa(*(struct in_addr *) &address));
                    fclose(fp);
                    return def_gateway;
                }
            }

        }
        fclose(fp);
    }

    return def_gateway;

}

#endif // !WIN32