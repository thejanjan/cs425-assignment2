#ifndef CLIENT_H
#define CLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "duckchat.h"
#include "raw.h"
#include "utils.h"

/*
 * Local Channel State
 */

struct ChannelRef {
    char *channelName = NULL;
    struct ChannelRef *_next = NULL;
};

static char current_channel[CHANNEL_MAX];
static struct ChannelRef *channelList = NULL;

bool is_channel_name_real(const char *channelName);

void add_channel(const char *channelName) {
	/* Adds a channel to local state. */
	strcpy(current_channel, channelName);
	if (is_channel_name_real(channelName)) return;
	if (channelList == NULL) {
		channelList = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
		channelList->channelName = strdup(channelName);
		channelList->_next = NULL;
		return;
	}

	// Find the end of the list.
	struct ChannelRef *channelRef = channelList;
	while ((channelRef->_next) != NULL)
		channelRef = channelRef->_next;

	channelRef->_next = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
	channelRef->_next->channelName = strdup(channelName);
	channelRef->_next->_next = NULL;
}

void remove_channel(const char *channelName) {
	/* Removes a channel from local state. */
	if (channelList == NULL) return;
	if (!is_channel_name_real(channelName)) return;
	if (strcmp(channelName, "Common") == 0) return;
	if (strcmp(channelName, current_channel) == 0)
		strcpy(current_channel, "Common");

	struct ChannelRef *channelRef = channelList;
	struct ChannelRef *lastRef = NULL;
	while (channelRef != NULL) {
		// Is this the channel?
		if (strcmp(channelName, channelRef->channelName) == 0) {
			// Is this the first one?
			if (lastRef == NULL)
				channelList = channelRef->_next;
			else
				lastRef->_next = channelRef->_next;
			free(channelRef->channelName);
			free(channelRef);
			return;
		}

		// Go to the next one.
		lastRef = channelRef;
		channelRef = channelRef->_next;
	}
}

bool is_channel_name_real(const char *channelName) {
	struct ChannelRef *channelRef = channelList;
	while (channelRef != NULL) {
		if (channelRef->channelName == NULL) return false;
		if (strcmp(channelName, channelRef->channelName) == 0) return true;
		channelRef = channelRef->_next;
	}
	return false;
}

void cleanup_channels() {
	if (channelList == NULL) return;
	struct ChannelRef *channelRef = channelList;
	while (channelRef != NULL) {
		if (channelRef->channelName != NULL)
			free(channelRef->channelName);
		struct ChannelRef *nextRef = channelRef->_next;
		free(channelRef);
		channelRef = nextRef;
	}
}


#endif