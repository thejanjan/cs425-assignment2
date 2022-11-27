#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "duckchat.h"

/*
 * Networking Utility Functions
 */

void fprintaddr(FILE *stream, struct sockaddr_in *address) {
    /* Prints the values of a given address onto a filestream. */
    fprintf(stream, "-=- address -=-\n");
    fprintf(stream, "sin_family: %hi\n",  address->sin_family);
    fprintf(stream, "sin_port:   %hu\n",  address->sin_port);
    fprintf(stream, "address:    %s\n",   inet_ntoa(address->sin_addr));
}

void fprintip(FILE *stream, struct sockaddr_in *address) {
    /* Prints an IP address directly to a stream. */
    // char buffer[INET_ADDRSTRLEN];
    // inet_ntop( AF_INET, &(address->sin_addr), buffer, sizeof( buffer ));
    char *ip = inet_ntoa(address->sin_addr);
    char port_str[6];
    sprintf(port_str, "%d", ntohs(address->sin_port));
    fprintf(stream, "%s:%s", ip, port_str);
}

void print_addresses(sockaddr_in *addressA, sockaddr_in *addressB) {
    // fprintaddr(stdout, addressA);
    // fprintaddr(stdout, addressB);
    fprintip(stdout, addressA); printf(" ");
    fprintip(stdout, addressB); printf(" ");
}

/*
 * Datagram Helpers
 */

int get_channel_list_datagram_size(void *datagram) {
    // Gets the size of the channel list datagram.
    struct text_list *textDatagram = (struct text_list *)datagram;
    return sizeof(struct text_list) + (sizeof(struct channel_info) * (textDatagram->txt_nchannels));
}

int get_who_datagram_size(void *datagram) {
    struct text_who *whoDatagram = (struct text_who *)datagram;
    return sizeof(struct text_who) + (sizeof(struct user_info) * (whoDatagram->txt_nusernames));
}

/*
 * Misc
 */

void scrub_channel_name(char name[CHANNEL_MAX]) {
    for (int i = 0; i < CHANNEL_MAX; i++)
        if (name[i] == '\n') name[i] = '\0';
    name[CHANNEL_MAX - 1] = '\0';  // sanity check
}

void scrub_chat_msg(char msg[SAY_MAX]) {
    for (int i = 0; i < SAY_MAX; i++)
        if (msg[i] == '\n') msg[i] = '\0';
    msg[SAY_MAX - 1] = '\0';  // sanity check
}

#endif
