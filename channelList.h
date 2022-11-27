#ifndef _CHANNELLIST_H_
#define _CHANNELLIST_H_

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "utils.h"

typedef struct channellist ChannelList;
const ChannelList *ChannelList_create();


struct channellist {
    void *self;
    void (*cleanup)(const ChannelList *cl);
    bool (*add_channel)(const ChannelList *cl, char *channelName);
    bool (*remove_channel)(const ChannelList *cl, char *channelName);
    bool (*has_channel)(const ChannelList *cl, char *channelName);

    // State Management
    bool (*is_valid)(const ChannelList *cl);
    void (*renew)(const ChannelList *cl, char *channelName);
    void (*age)(const ChannelList *cl);
    const ChannelList *(*find_outdated_channel)(const ChannelList *cl);
};


typedef struct channellistdata {
    char *channelName;
    int state = 2;
    const ChannelList *next;
    const ChannelList *prev;
} ChannelListData;


static void channel_list_cleanup(const ChannelList *cl) {
    ChannelListData *cld = (ChannelListData *)cl->self;
    const ChannelList *next = cld->next;

    // Free ourselves
    free(cld->channelName);
    free((void *)cl);
    free(cld);

    if (next != NULL)
        // Cleanup next
        next->cleanup(next);
}

static bool channel_list_add_channel(const ChannelList *cl, char *channelName) {
    // add a channel to this CL
    if (cl->has_channel(cl, channelName))
        // this channel is already in the channelList, so ignore.
        // NOTE - this is not performant and makes this operation NlogN,
        // but its fine w/e
        return false;

    // now look to adding it
    ChannelListData *cld = (ChannelListData *)cl->self;
    const ChannelList *next = cld->next;
    if (next != NULL) {
        // add it to the end
        return cl->add_channel(cl, channelName);
    } else {
        if (cld->channelName == NULL) {
            // this is a singleton channel -- set this ones in particular
            cld->channelName = strdup(channelName);
            cld->state = 2;
        } else {
            // set the channel on the end
            const ChannelList *end = ChannelList_create();
            cld->next = end;

            ChannelListData *endd = (ChannelListData *)end->self;
            endd->channelName = strdup(channelName);
            endd->prev = cl;
        }
        return true;
    }
}

static bool channel_list_remove_channel(const ChannelList *cl, char *channelName) {
    // removes a channel from this CL
    ChannelListData *cld = (ChannelListData *)cl->self;
    const ChannelList *next = cld->next;
    const ChannelList *prev = cld->prev;

    // no channel name? bye
    if (cld->channelName == NULL) return false;

    // compare the name with this one
    if (strcmp(channelName, cld->channelName) == 0) {
        // the names are identical, so remove it
        if (cld->next == NULL) {

            // we are removing the very end piece
            if (cld->prev != NULL) {
                // and must inform the prior
                ChannelListData *prev_d = (ChannelListData *)prev->self;
                prev_d->next = NULL;
                cl->cleanup(cl);

            } else {
                // actually -- we are removing all channels, period. reset to empty state
                free(cld->channelName);
                cld->channelName = NULL;
            }

        } else {

            // we are not removing the last piece
            ChannelListData *next_d = (ChannelListData *)next->self;
            if (cld->prev != NULL) {
                // and we must reconnect with the prior
                ChannelListData *prev_d = (ChannelListData *)prev->self;
                prev_d->next = next;
                next_d->prev = prev;

                cld->next = NULL;
                cl->cleanup(cl);

            } else {
                // we are actually removing the first channel... hmm...
                // lets go ahead and replace ourselves with the next in line.
                cld->channelName = next_d->channelName;
                cld->next = next_d->next;
                cld->prev = NULL;;

                next_d->next = NULL;
                next->cleanup(next);
            }

        }
        return true;

    } else {
        // check the next one
        if (next != NULL) {
            return next->has_channel(next, channelName);
        } else {
            // could not find it
            return false;
        }
    }
}

static bool channel_list_has_channel(const ChannelList *cl, char *channelName) {
    // removes a channel from this CL
    ChannelListData *cld = (ChannelListData *)cl->self;

    if ((cld->channelName) == NULL) return false;

    // compare the name with this one
    if (strcmp(channelName, cld->channelName) == 0) {
        // the names are identical, so we have it
        return true;
    } else {
        // check the next one
        const ChannelList *next = cld->next;
        if (next != NULL) {
            return next->has_channel(next, channelName);
        } else {
            // could not find it
            return false;
        }
    }
}

static bool channel_list_is_valid(const ChannelList *cl) {
    // Determines if this ChannelList is still valid for use.
    ChannelListData *cld = (ChannelListData *)cl->self;
    return (cld->state > 0);
}

static void channel_list_renew(const ChannelList *cl, char *channelName) {
    /* Renews a channelName within this channelList. */
    ChannelListData *cld = (ChannelListData *)cl->self;
    if ((cld->channelName) == NULL) return;

    // Compare the name with this one.
    if ((cl->is_valid(cl)) && (strcmp(channelName, cld->channelName) == 0)) {
        // This is the one we have to renew -- so renew it.
        cld->state = 2;
    } else {
        // Attempt to renew the next in line.
        const ChannelList *next = cld->next;
        if (next != NULL)
            next->renew(next, channelName);
    }
}

static void channel_list_age(const ChannelList *cl) {
    /*
      Ages the entire channel list.
      If we return True, we have removed a channel.
    */
    ChannelListData *cld = (ChannelListData *)cl->self;

    // Age this one if it is still valid.
    if (cl->is_valid(cl))
        // Reduce the state.
        cld->state = (cld->state) - 1;

    // Attempt to age the next in line.
    const ChannelList *next = cld->next;
    if (next != NULL)
        next->age(next);
}

static const ChannelList *channel_list_find_outdated_channel(const ChannelList *cl) {
    /*
      Goes through the entire channel list and attempts to find an outdated channel.
    */
    ChannelListData *cld = (ChannelListData *)cl->self;

    // Do nothing if this is empty.
    if (cld->channelName == NULL) return NULL;

    // If this channel is no longer valid, return it.
    if (!(cl->is_valid(cl)))
        return cl;

    // Attempt to find the next in line.
    const ChannelList *next = cld->next;
    if (next != NULL)
        return next->find_outdated_channel(next);
    
    // No channels are out of date.
    return NULL;
}

const ChannelList *ChannelList_create() {
    ChannelList *cl = (ChannelList *)malloc(sizeof(ChannelList));
    memset(cl, 0, sizeof(ChannelList));

    ChannelListData *cld = (ChannelListData *)malloc(sizeof(ChannelListData));
    cld->channelName = NULL;
    cld->state = 2;
    cld->next = NULL;
    cld->prev = NULL;

    *cl = {NULL, channel_list_cleanup, channel_list_add_channel, channel_list_remove_channel, channel_list_has_channel,
           channel_list_is_valid, channel_list_renew, channel_list_age, channel_list_find_outdated_channel};
    cl->self = (void *)cld;
    return cl;
}

#endif /* _CHANNELLIST_H_ */