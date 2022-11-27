#ifndef _TOPOLOGY_H_
#define _TOPOLOGY_H_

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <cerrno>

#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "utils.h"
#include "duckchat.h"
#include "server.h"

/*
 * topology ADT
 */

#define TOPOLOGY_MAX_SIZE 100
#define TOPOLOGY_MAX_CHANNELS 100
#define TOPOLOGY_MAX_ID_POOL 2000

typedef struct topology Topology;
typedef struct serverdata ServerData;

struct topology {
	void *self;
	void (*cleanup)(const Topology *tp);
	int  (*get_size)(const Topology *tp);
	int  (*get_socket)(const Topology *tp, int index);
	bool (*add_address)(const Topology *tp, int socket, char *hostname, char *port);
	ServerData *(*find_server)(const Topology *tp, struct sockaddr_in *address);

	// renewal management
	bool (*renew)(const Topology *tp, struct sockaddr_in *serverAddr, struct ChannelRef *channelList);

	// topology calls
	bool (*s2s_join_send)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName);
	bool (*s2s_leave_send)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName);
	bool (*s2s_say_send)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *username, char *channelName, char *text, long long id);

	// topology receives
	bool (*s2s_join_recv)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName);
	bool (*s2s_leave_recv)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName);
	bool (*s2s_say_recv)(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, long long id, char *username, char *channelName, char *text);

	// id management
	void (*id_store)(const Topology *tp, long long id);
	bool (*id_has)(const Topology *tp, long long id);
};

#include "channelList.h"

typedef struct serverdata {
	struct sockaddr_in *address;
	int socket;
	const ChannelList *channelList;
} ServerData;


typedef struct topologydata {
    ServerData *serverTopology[TOPOLOGY_MAX_SIZE];
    int size;

    long long recentIds[TOPOLOGY_MAX_ID_POOL];
    int idPoolSize;
} TopologyData;

static void topology_cleanup(const Topology *tp) {
	free(tp->self);
	free((void *)tp);
}

static int topology_get_size(const Topology *tp) {
	TopologyData *tpd = (TopologyData *)(tp->self);
	return tpd->size;
}

static int topology_get_socket(const Topology *tp, int index) {
	TopologyData *tpd = (TopologyData *)(tp->self);
	return tpd->serverTopology[index]->socket;
}

static bool topology_add_address(const Topology *tp, int socket, char *hostname, char *port) {
    // get our tp data
    TopologyData *tpd = (TopologyData *)(tp->self);

    // make a new address
    struct sockaddr_in *serverAddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    memset(serverAddr, 0, sizeof(struct sockaddr_in));
    serverAddr->sin_family = AF_INET;
    serverAddr->sin_port = htons(atoi(port));

    struct hostent *he;
    if ((he = gethostbyname(hostname)) == NULL) {
        fprintf(stderr, "Error resolving hostname\n");
        exit(1);
    }
    memcpy(&(serverAddr->sin_addr), he->h_addr_list[0], he->h_length);

    // make serverdata, populate it
    ServerData *sd = (ServerData *)malloc(sizeof(ServerData));
    memset(sd, 0, sizeof(ServerData));

    sd->address = serverAddr;
    sd->socket = socket;
    sd->channelList = ChannelList_create();

    // add the address to the struct
    int current_size = tpd->size;
    (tpd->serverTopology)[current_size] = sd;
    tpd->size += 1;

    // everything seems good
    return true;
}

static ServerData *topology_find_server(const Topology *tp, struct sockaddr_in *address) {
	/* Locates a server given an address. */
	if (address == NULL) return NULL;
	TopologyData *tpd = (TopologyData *)tp->self;
	for (int i = 0; i < tpd->size; i++) {
		// Check if this server has the topology we need.
		ServerData *sd = (tpd->serverTopology)[i];
		if (cmpaddress(*(sd->address), *address)) return sd;
	}
	// Could not find the server.
	return NULL;
}
static bool topology_renew(const Topology *tp, struct sockaddr_in *serverAddr, struct ChannelRef *currentChannel) {
	/* 
	 * Sends the renew request for the server topology.
	 * Also handles the leaves for old servers.
	 */
	// First, renew all of our channels.
	while (currentChannel != NULL) {
		// If no channel name -- bye
		if (currentChannel->_this == NULL) break;
		if (currentChannel->_this->channelName == NULL) break;

		// Send a join for this channel.
		tp->s2s_join_send(tp, serverAddr, NULL, currentChannel->_this->channelName);
		
		// Move onto next channel.
		currentChannel = currentChannel->_next;
	}

	// Now, we need to review our server topology and age everything.
	TopologyData *tpd = (TopologyData *)tp->self;
	for (int i = 0; i < tpd->size; i++) {
		// Get the server topology for this.
		ServerData *sd = (tpd->serverTopology)[i];

		// Age its topology.
		const ChannelList *cl = sd->channelList;
		cl->age(cl);

		// Clean up any empty servers.
		const ChannelList *outdated = NULL;
		while ((outdated = cl->find_outdated_channel(cl)) != NULL) {
			// We have an outdated channel -- remove it.
			ChannelListData *outdatedData = (ChannelListData *)outdated->self;

			// Treat this as receiving a leave call -- this will also clean up the channel structure.
			tp->s2s_leave_recv(tp, serverAddr, sd->address, outdatedData->channelName);
		}
	}

	// Renewal complete.
	return true;
}
static bool topology_s2s_join_send(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName) {
	// Send this to all adjacent servers.
	TopologyData *tpd = (TopologyData *)tp->self;
	for (int i = 0; i < tpd->size; i++) {
		// Print what we are sending to this server.
		ServerData *sd = (tpd->serverTopology)[i];
		if (address != NULL) {
			// Avoid sending this back to a server.
			if (cmpaddress(*(sd->address), *address)) continue;
		}
		print_addresses(serverAddr, sd->address);
        printf("send S2S Join %s\n", channelName);

        // Add this channel to the channelList for this server.
        sd->channelList->add_channel(sd->channelList, channelName);

        // Create our datagram.
        request_server_join *datagram = (request_server_join *)malloc(sizeof(request_server_join));
        memcpy((void *)(&datagram->address), serverAddr, sizeof(struct sockaddr_in));
        datagram->req_type = S2S_JOIN;
        memcpy(datagram->req_channel, channelName, sizeof(char) * CHANNEL_MAX);

        // Send our datagram over.
        int result = sendto(
            sd->socket, datagram, sizeof(request_server_join), MSG_DONTWAIT,
            (struct sockaddr *)(sd->address), sizeof(struct sockaddr_in)
        );
        if (result == -1) fprintf(stderr, "S2S join send failure. (%d)\n", result);
	}
	return true;
}
static bool topology_s2s_leave_send(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName) {
	TopologyData *tpd = (TopologyData *)tp->self;

	if (address == NULL) {
		// We are sending a leave to ALL servers.
		for (int i = 0; i < tpd->size; i++) {
			// Print what we are sending to this server.
			ServerData *sd = (tpd->serverTopology)[i];
			print_addresses(serverAddr, sd->address);
	        printf("send S2S Leave %s\n", channelName);

	        // Take this channel out of the server's routing table.
	        sd->channelList->remove_channel(sd->channelList, channelName);

	        // Create our datagram.
	        request_server_leave *datagram = (request_server_leave *)malloc(sizeof(request_server_leave));
	        memcpy((void *)(&datagram->address), serverAddr, sizeof(struct sockaddr_in));
	        datagram->req_type = S2S_LEAVE;
	        memcpy(datagram->req_channel, channelName, sizeof(char) * CHANNEL_MAX);

	        // Send our datagram over.
	        int result = sendto(
	            sd->socket, datagram, sizeof(request_server_leave), MSG_DONTWAIT,
	            (struct sockaddr *)(sd->address), sizeof(struct sockaddr_in)
	        );
	        if (result == -1) fprintf(stderr, "S2S leave send failure. (%d)\n", result);
		}
	} else {
		// We are only sending a leave to one server.
		ServerData *sd = tp->find_server(tp, address);
		if (sd == NULL) return false;

		print_addresses(serverAddr, sd->address);
        printf("send S2S Leave %s\n", channelName);

        // Take this channel out of the server's routing table.
        sd->channelList->remove_channel(sd->channelList, channelName);

        // Create our datagram.
        request_server_leave *datagram = (request_server_leave *)malloc(sizeof(request_server_leave));
        memcpy((void *)(&datagram->address), serverAddr, sizeof(struct sockaddr_in));
        datagram->req_type = S2S_LEAVE;
        memcpy(datagram->req_channel, channelName, sizeof(char) * CHANNEL_MAX);

        // Send our datagram over.
        int result = sendto(
            sd->socket, datagram, sizeof(request_server_leave), MSG_DONTWAIT,
            (struct sockaddr *)(sd->address), sizeof(struct sockaddr_in)
        );
        if (result == -1) fprintf(stderr, "S2S leave send failure. (%d)\n", result);
	}

	// Mission success.
	return true;
}
static bool topology_s2s_say_send(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *username, char *channelName, char *text, long long id) {
	// Create our datagram.
    request_server_say *datagram = (request_server_say *)malloc(sizeof(request_server_say));
    memcpy((void *)(&datagram->address), serverAddr, sizeof(struct sockaddr_in));
    datagram->req_type = S2S_SAY;
    memcpy(datagram->txt_username, username, sizeof(char) * USERNAME_MAX);
    memcpy(datagram->txt_channel, channelName, sizeof(char) * CHANNEL_MAX);
    memcpy(datagram->txt_text, text, sizeof(char) * SAY_MAX);

    if (id == 0) {
        // give it a random id
        int randomData = open("/dev/urandom", O_RDONLY);
        if (randomData < 0) {
        	// Read error ... fallback to id 0
        	datagram->id = 0;
        	printf("WARNING: Could not read random data to generate say ID!\n");
        } else {
        	long long id;
        	ssize_t result = read(randomData, &id, sizeof(long long));
        	if (result < 0) {
        		// BAD
        		datagram->id = 0;
        	} else datagram->id = id;
        	close(randomData);
        }
        // store the id in pool
    	tp->id_store(tp, datagram->id);
    } else {
    	// otherwise use the one we were passed
    	datagram->id = id;
    }

    // Forward it to EVERY SERVER with the channel.
    bool hasSent = false;
    // printf("S2S SAY - Forwarding message..\n");
	TopologyData *tpd = (TopologyData *)tp->self;
	for (int i = 0; i < tpd->size; i++) {
		// Print what we are sending to this server.
		ServerData *sd = (tpd->serverTopology)[i];

		// Do not forward it back.
		if (address != NULL) {
			// Avoid sending this back to a server.
			if (cmpaddress(*(sd->address), *address)) continue;
		}

		// Is this server sendable?
		if (!(sd->channelList->has_channel(sd->channelList, channelName))) {
			// printf("S2S SAY - Attempted to send message to a server, but they were not present in routing table\n");
			continue;
		}

		// OK, begin print and start sending!
		print_addresses(serverAddr, sd->address);
        // printf("send S2S Say %s %s \"%s\"\n", username, channelName, text);
        hasSent = true;

        // Send our datagram over.
        int result = sendto(
            sd->socket, datagram, sizeof(request_server_say), MSG_DONTWAIT,
            (struct sockaddr *)(sd->address), sizeof(struct sockaddr_in)
        );
        if (result == -1) fprintf(stderr, "S2S say send failure. (%d)\n", result);
	}

	// cleanup
	free(datagram);
	return hasSent;
}
static bool topology_s2s_join_recv(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName) {
    // Does the channel exist for us?
	print_addresses(serverAddr, address);
    printf("recv S2S Join %s\n", channelName);

    // Get server data.
    ServerData *sd = tp->find_server(tp, address);

    // We need to find the topology data from the server that sent us this join call.
    // We'll have to add them to our routing table for this channel.
    if (sd != NULL)
    	sd->channelList->add_channel(sd->channelList, channelName);

    // If we have already joined this channel, ignore this part.
    if (get_channel(channelName, false) == NULL) {
    	// Create the channel for ourselves locally.
	    get_channel(channelName, true);

	    // We need to figure out if we have anybody to route this call to.
	    // If we do, then we need to add that server to our routing table.
	    // This logic is handled by s2s_join_send.
	    tp->s2s_join_send(tp, serverAddr, address, channelName);
    }

    // We need to renew this address for the server.
    if (sd != NULL)
    	sd->channelList->renew(sd->channelList, channelName);

    // We're done here.
    return true;
}
static bool topology_s2s_leave_recv(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, char *channelName) {
	// First, go ahead and print that we received this call.
	print_addresses(serverAddr, address);
    printf("recv S2S Leave %s\n", channelName);

    // When we receive this call, we need to go ahead and remove the channel from the address's routing table.
    ServerData *sd = tp->find_server(tp, address);
    if (sd != NULL) {
    	sd->channelList->remove_channel(sd->channelList, channelName);
    	return true;
    }

    // We couldn't remove the channel, so return failure.
    return false;
}
static bool topology_s2s_say_recv(const Topology *tp, struct sockaddr_in *serverAddr, struct sockaddr_in *address, long long id, char *username, char *channelName, char *text) {
	// Print the receival addresses.
	print_addresses(serverAddr, address);
    printf("recv S2S Say %s %s \"%s\"\n", username, channelName, text);

	// Have we already received this ID?
	if (tp->id_has(tp, id)) {
		// If we have, then this message is a duplicate -- we can break
		// off from the tree from this address by sending that server a leave call.
		tp->s2s_leave_send(tp, serverAddr, address, channelName);
		// printf("Declining S2S Say Recv - duplicate\n");
		return false;
	}

    // is anyone here even in this channel?
    struct Channel *channel = get_channel(channelName, false);
    if (channel == NULL) {
    	// channel does not even exist for us, ignore the call
    	// printf("Declining S2S Say Recv - channel does not exist\n");
    	return false;
    }

    // store and forward (get it??)
    tp->id_store(tp, id);
    // printf("Accepting S2S Say Recv - sending forward\n");
    bool hasSent = tp->s2s_say_send(tp, serverAddr, address, username, channelName, text, id);
    if ((hasSent == false) && (channel->userCount == 0)) {
    	// We couldn't send it to anyone, so reply with a leave.
    	tp->s2s_leave_send(tp, serverAddr, NULL, channelName);
    	cleanup_channel(channel);
    	return false;
    }
    // All is well
    return true;
}
static void topology_id_store(const Topology *tp, long long id) {
	/* stores an ID in the id pool */
	// get the data
	TopologyData *tpd = (TopologyData *)(tp->self);

	// reset the pool size if overflow
	if ((tpd->idPoolSize) >= TOPOLOGY_MAX_ID_POOL) tpd->idPoolSize = 0;

	// store in the pool
	(tpd->recentIds)[tpd->idPoolSize] = id;
	tpd->idPoolSize = (tpd->idPoolSize) + 1;
}
static bool topology_id_has(const Topology *tp, long long id) {
	/* determines if the ID is in the pool */
	// get the data
	TopologyData *tpd = (TopologyData *)(tp->self);

	// iterate over it
	for (int i = 0; i < (tpd->idPoolSize); i++)
		if (id == ((tpd->recentIds)[i])) return true;

	// i have never heard of you in my life
	return false;
}

const Topology *Topology_create() {
    Topology *tp = (Topology *)malloc(sizeof(Topology));
    memset(tp, 0, sizeof(Topology));

    TopologyData *tpd = (TopologyData *)malloc(sizeof(TopologyData));
	memset(tpd, 0, sizeof(TopologyData));

    *tp = {NULL, topology_cleanup, topology_get_size, topology_get_socket, topology_add_address,
    	   topology_find_server, topology_renew,
    	   topology_s2s_join_send, topology_s2s_leave_send, topology_s2s_say_send,
    	   topology_s2s_join_recv, topology_s2s_leave_recv, topology_s2s_say_recv,
    	   topology_id_store, topology_id_has};
    tp->self = (void *)tpd;
    return tp;
}

#endif /* _TOPOLOGY_H_ */