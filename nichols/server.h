#ifndef SERVER_H
#define SERVER_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <time.h>

#include "duckchat.h"
#include "utils.h"

// Prototypes for topology
struct Channel {
    char *channelName;
    int userCount = 0;
};
struct ChannelRef {
    struct Channel *_this = NULL;
    struct ChannelRef *_next = NULL;
};
struct Channel *get_channel(char name[CHANNEL_MAX], bool create);
void cleanup_channel(struct Channel *channel);
bool cmpaddress(sockaddr_in addressA, sockaddr_in addressB) {
    // Compares two addresses.
    return ((addressA.sin_port == addressB.sin_port) && (addressA.sin_addr.s_addr == addressB.sin_addr.s_addr));
}

#include "topology.h"

/*
 * Prototyping
 */

struct User;

void remove_user_from_channel(struct User *user, struct Channel *channel);
bool is_user_in_channel(struct User *user, struct Channel *channel);
void heartbeat_user(struct User *user);

/* 
 * Server-Sided Structures
 */

struct AddressRef {
    struct sockaddr_in *_this = NULL;
    struct AddressRef *_next = NULL;
};

struct UserRef {
    struct User *_this = NULL;
    struct UserRef *_next = NULL;
};

struct User {
    struct sockaddr_in *address;
    char *username;
    time_t expiresAt;
    struct ChannelRef *channels = NULL;
};

/*
 * State
 */

static struct UserRef *userList = NULL;
static struct ChannelRef *channelList = NULL;

/*
 * Address Management
 */

struct AddressRef *create_address_list(struct sockaddr_in *startAddress) {
    /* Allocates an address reference linked list. */
    struct AddressRef *addressRef = (struct AddressRef *)malloc(sizeof(struct AddressRef));
    addressRef->_this = startAddress;
    addressRef->_next = NULL;
    return addressRef;
}

bool add_address_to_list(struct AddressRef *addressRef, struct sockaddr_in *address) {
    /* 
     * Adds an address to the ref list.
     * Return true if successful, false if not.
     */
    while (addressRef->_this != NULL) {
        // The current ref has an address allocated already.
        // We need to go to the next one.
        if (addressRef->_next == NULL)
            addressRef->_next = create_address_list(NULL);
        addressRef = addressRef->_next;
    }

    // Now add the ref.
    struct sockaddr_in *newAddress = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    memcpy(newAddress, address, sizeof(struct sockaddr_in));
    addressRef->_this = newAddress;
    return true;
}

void free_address_list(struct AddressRef *addressRef) {
    /* Frees an address reference linked list. */
    while (addressRef != NULL) {
        // Free the address here.
        if (addressRef->_this != NULL)
            free((void *)addressRef->_this);

        // Go to the next address ref.
        struct AddressRef *lastRef = addressRef;
        addressRef = addressRef->_next;
        free(lastRef);
    }
}

/*
 * User Management
 */

bool create_user(struct sockaddr_in address, char *name) {
    /* 
     * Creates a new user.
     * Returns true if successful, false if not.
     */
    // Allocate memory for the user.
    struct User *newUser = (struct User *)malloc(sizeof(struct User));
    struct ChannelRef *channelRef = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
    channelRef->_this = NULL;
    channelRef->_next = NULL;

    struct sockaddr_in *userAddress = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));

    // Put the user's fields together.
    memcpy(userAddress, &address, sizeof(struct sockaddr_in));
    newUser->address = userAddress;
    newUser->username = (char *)malloc(sizeof(char) * USERNAME_MAX);
    strcpy(newUser->username, name);
    newUser->channels = channelRef;

    // Put the user in the user list.
    struct UserRef *userRef = userList;
    if (userList->_this != NULL) {
        // Find the end user ref.
        while (userRef->_next != NULL) {userRef = userRef->_next;}

        // Add a user ref to the end. Our user goes there!
        struct UserRef *newUserList = (struct UserRef *)malloc(sizeof(struct UserRef));
        newUserList->_next = NULL;
        userRef->_next = newUserList;
        userRef = newUserList;
    }

    // Set the user ref.
    userRef->_this = newUser;

    // win
    heartbeat_user(newUser);
    return true;
}

User *get_user(struct sockaddr_in address) {
    /*
     * Looks for a user.
     */
    // Iterate over users.
    struct UserRef *userRef = userList;
    if (userRef == NULL) return NULL;
    while (userRef->_this != NULL) {
        // Is this the user?
        if ((userRef->_this->address) != NULL) {
            if (cmpaddress(address, *(userRef->_this->address))) {
                // We found it!
                return userRef->_this;
            }
        }

        // Go to the next user, if it exists.
        if (userRef->_next == NULL) break;
        userRef = userRef->_next;
    }

    // User not found!
    return NULL;
}

void cleanup_user_attribs(User *user) {
    // Cleanup the user attribs.
    // printf("%s has left the server.\n", user->username);
    free((void *)(user->username));
    free((void *)(user->address));

    // Remove them from all channels manually.
    struct ChannelRef *channelRef = user->channels;
    while (channelRef != NULL) {
        if (channelRef->_this == NULL) break;
        struct Channel *channel = channelRef->_this;
        channelRef = channelRef->_next;
        remove_user_from_channel(user, channel);
    }

    // Cleanup the user.
    free((void *)user);
}

bool remove_user(struct sockaddr_in address) {
    /*
     * Removes a user.
     * Returns true if successful, false if not.
     */
    struct UserRef *userRef = userList;
    struct UserRef *lastRef = NULL;
    while (userRef != NULL) {
        if (userRef->_this == NULL) break;
        struct User *user = userRef->_this;

        // If this is the user, clean everything up.
        if (cmpaddress(address, *(user->address))) {
            // Cleanup user attributes.
            cleanup_user_attribs(user);

            // Now address the linked list.
            if (userRef == userList) {
                // The first element is used.
                if (userList->_next == NULL) {
                    // The only user was removed.
                    userList->_this = NULL;
                } else {
                    // The user list is gonna be replaced with the next user list.
                    free(userList);
                    userList = userRef;
                }
            } else {
                // The non-first element is used.
                // Link the last reference to the next one.
                lastRef->_next = userRef->_next;
                free(userRef);
            }
        } else {
            // Check the next one.
            lastRef = userRef;
            userRef = userRef->_next;
        }
    }

    // User could not be found.
    return false;
}

void heartbeat_user(struct User *user) {
    /* Sets the expiry date on the User. */
    time_t expiryDate;
    time(&expiryDate);
    user->expiresAt = (expiryDate + SERVER_KEEPALIVE);
}

bool has_user_expired(struct User *user) {
    /* Checks if a user has expired. */
    time_t currentTime;
    time(&currentTime);
    return (user->expiresAt) < currentTime;
}

int get_user_count() {
    /* Gets the number of users. */
    int userCount = 0;
    struct UserRef *currentUser = userList;
    while (currentUser != NULL) {
        // This user is SO REAL
        userCount += 1;

        // Find the next channel.
        currentUser = currentUser->_next;
    }
    return userCount;
}

void initialize_users() {
    // Create the channel list.
    struct UserRef *newUserList = (struct UserRef *)malloc(sizeof(struct UserRef));
    newUserList->_this = NULL;
    newUserList->_next = NULL;
    userList = newUserList;
}

void cleanup_users() {
    // Cleans up the user list.
    struct UserRef *userRef = userList;
    if (userList->_this != NULL) {
        while (userRef != NULL) {
            // Cleanup the user attribs.
            cleanup_user_attribs(userRef->_this);

            // Go to the next user ref.
            struct UserRef *nextRef = userRef->_next;
            free((void *)(userRef));
            userRef = nextRef;
        }
    } else {
        free(userList);
    }
}

/*
 * Channel Management
 */

struct Channel *get_channel(char name[CHANNEL_MAX], bool create) {
    /* 
     * Gets a channel. 
     * If it does not exist, it will create it if specified.
     */
    // Cleanup the channel name.
    scrub_channel_name(name);

    // Iterate over channels.
    struct ChannelRef *currChannelRef = channelList;
    if (channelList == NULL) {
        if (!create) 
            return NULL;
        else {
            // Create the initial channel list.
            struct ChannelRef *initialChannelList = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
            initialChannelList->_this = NULL;
            initialChannelList->_next = NULL;
            channelList = initialChannelList;
            currChannelRef = initialChannelList;
        }
    }

    while (currChannelRef->_this != NULL) {
        // Is this the channel?
        if (strcmp(name, currChannelRef->_this->channelName) == 0) {
            // We found it!
            return currChannelRef->_this;
        }

        // Go to the next channel, if it exists.
        if (currChannelRef->_next == NULL) break;
        currChannelRef = currChannelRef->_next;
    }

    // Uh oh ... the channel could not be found.
    if (create) {
        // Create a new one and add it to the channel list.
        struct Channel *newChannel = (struct Channel *)malloc(sizeof(struct Channel));

        // Populate the channel.
        newChannel->channelName = (char *)malloc(sizeof(char) * CHANNEL_MAX);
        newChannel->userCount = 0;
        strcpy(newChannel->channelName, name);

        if (channelList->_this != NULL) {
            // We need a new channelRef.
            struct ChannelRef *newChannelList = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
            currChannelRef->_next = newChannelList;
            newChannelList->_this = newChannel;
            newChannelList->_next = NULL;
        } else {
            // This is the first channel.
            channelList->_this = newChannel;
        }
        return newChannel;
    } else {
        // This channel is simply not real.
        return NULL;
    }
}

struct Channel *get_initial_channel() {
    if (channelList == NULL) {
        char *channelName = (char *)malloc(sizeof(char) * CHANNEL_MAX);
        strcpy(channelName, "Common");
        get_channel(channelName, true);
    }
    return channelList->_this;
}

int get_channel_count() {
    /* Gets the number of active channels. */
    int channelCount = 0;
    struct ChannelRef *currentChannel = channelList;
    while (currentChannel != NULL) {
        // This channel is SO REAL
        channelCount += 1;

        // Find the next channel.
        currentChannel = currentChannel->_next;
    }
    return channelCount;
}

void initialize_channels() {
    // Create the channel list.
    // struct ChannelRef *newChannelList = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
    // newChannelList->_this = NULL;
    // newChannelList->_next = NULL;
    // channelList = newChannelList;
    return;
}

void cleanup_channel(struct Channel *channel) {
    // Cleans up a channel from the channel list.
    struct ChannelRef *channelRef = channelList;
    struct ChannelRef *lastRef = NULL;
    while (channelRef != NULL) {
        // is this the channel we are looking for?
        if (channelRef->_this == channel) {
            // Clean up this channel.
            // We will need to scrub this channel.
            if ((lastRef == NULL) && (channelRef->_next == NULL)) {
                // We removed the only channel we were in.
                channelList = NULL;
            } else if (lastRef == NULL) {
                // We were in 2+ channels, but we removed the first one.
                channelList = channelRef->_next;
            } else {
                // We were in 2+ channels, and we did not remove the first.
                lastRef->_next = channelRef->_next;
            }

            // Clean up the channel ref.
            free((void *)channel->channelName);
            free((void *)channel);
            free(channelRef);
            return;
        }

        // Go to the next channel ref.
        lastRef = channelRef;
        channelRef = channelRef->_next;
    }
}

void cleanup_channels() {
    // Cleans up the channel list.
    struct ChannelRef *channelRef = channelList;
    while (channelRef != NULL) {
        // Cleanup the channel attribs.
        struct Channel *channel = channelRef->_this;
        free((void *)(channel->channelName));

        // Cleanup the channel.
        free((void *)channel);

        // Go to the next channel ref.
        struct ChannelRef *lastRef = channelRef;
        channelRef = channelRef->_next;
        free((void *)(lastRef));
    }
}

/*
 * User+Channel Management
 */

struct UserRef *get_users_in_channel(struct Channel *channel) {
    /*
     * Gets a UserRef linked list of all users in a channel.
     */
    struct UserRef *channelUserList = NULL;
    struct UserRef *channelUserListStart = NULL;

    // Go over all users.
    struct UserRef *checkUser = userList;
    while ((checkUser != NULL) && ((checkUser->_this) != NULL)) {
        // Is this user in a channel?
        if (is_user_in_channel(checkUser->_this, channel)) {
            // The user is in this channel.
            struct UserRef *newUserRef = (struct UserRef *)malloc(sizeof(struct UserRef));
            newUserRef->_this = checkUser->_this;
            newUserRef->_next = NULL;

            // Attach it to the existing list.
            if (channelUserList == NULL) {
                channelUserList = newUserRef;
                channelUserListStart = newUserRef;
            } else {
                channelUserList->_next = newUserRef;
                channelUserList = channelUserList->_next;
            }
        }

        // Go to the next user.
        checkUser = checkUser->_next;
    }

    // Return the list.
    return channelUserListStart;
}

void free_user_ref(struct UserRef *userRef) {
    // Frees a userref struct.
    while (userRef != NULL) {
        struct UserRef *nextRef = userRef->_next;
        free((void *)userRef);
        userRef = nextRef;
    }
}

bool is_user_in_channel(struct User *user, struct Channel *channel) {
    /*
     * Is this user in the given channel?
     * Returns true if so.
     */
    // We need to get to the end of their channel list.
    struct ChannelRef *channelRef = user->channels;

    // If this first one is empty, they are in no channels.
    if (channelRef->_this == NULL)
        return false;
    else
        while (1) {
            // Is this the same channel?
            if (channel == channelRef->_this)
                // They are, so the user is here.
                return true;

            // Let's move to the next ref.
            if (channelRef->_next == NULL)
                // jk there is no next ref
                return false;
            channelRef = channelRef->_next;
        }
}

void add_user_to_channel(struct User *user, struct Channel *channel) {
    /* 
     * Adds a user to a channel.
     */
    // We need to get to the end of their channel list.
    struct ChannelRef *channelRef = user->channels;

    // If this first one is empty, do something now.
    if (channelRef->_this == NULL) {
        channelRef->_this = channel;
        channel->userCount = channel->userCount + 1;
        return;
    } else {
        while (1) {
            if (channelRef->_this == NULL) {
                // We can add a channel to this ref!.
                channelRef->_this = channel;
                channel->userCount = channel->userCount + 1;
                return;
            }

            // Let's move to the next ref.
            if (channelRef->_next == NULL) {
                struct ChannelRef *nextRef = (struct ChannelRef *)malloc(sizeof(struct ChannelRef));
                nextRef->_this = NULL;
                nextRef->_next = NULL;
                channelRef->_next = nextRef;
            }
            channelRef = channelRef->_next;
        }
    }
}

void remove_user_from_channel(struct User *user, struct Channel *channel) {
    /* 
     * Removes a user to a channel.
     * Returns True if successful, False if they were not there.
     */
    // We need to get to the end of their channel list.
    struct ChannelRef *channelRef = user->channels;
    struct ChannelRef *lastRef = NULL;

    // If this first one is empty, this channel aint here.
    if (channelRef->_this == NULL) {
        return;
    } else {
        while (1) {
            if (channelRef->_this == channel) {
                // Looks like we have found the necessary channel.
                // We will need to scrub this channel.
                if ((lastRef == NULL) && (channelRef->_next == NULL)) {
                    // We removed the only channel we were in. Ok!
                    channelRef->_this = NULL;
                    return;
                } else if (lastRef == NULL) {
                    // We were in 2+ channels, but we removed the first one.
                    user->channels = channelRef->_next;
                } else {
                    // We were in 2+ channels, and we did not remove the first.
                    lastRef->_next = channelRef->_next;
                }

                // cleanup !!
                free((void *)channelRef);

                // remove one user from this channel, cleanup if necessary
                channel->userCount = channel->userCount - 1;
                if ((channel->userCount) <= 0) cleanup_channel(channel);
                return;
            }

            // Let's move to the next ref.
            if (channelRef->_next == NULL)
                // it DOESNT EXIST!!!!!!!11
                return;
            lastRef = channelRef;
            channelRef = channelRef->_next;
        }
    }
}

/*
 * Channel-Related Datagram Magic
 */

void *make_channel_list_datagram() {
    // Figure out our channel data.
    int channelCount = get_channel_count();
    struct channel_info **channelInfoArray = (struct channel_info **)malloc(sizeof(struct channel_info*) * channelCount);
    struct ChannelRef *currentChannel = channelList;
    for (int i = 0; i < channelCount; i++) {
        // Make a channel info here.
        struct channel_info *thisChannelinfo = (struct channel_info *)malloc(sizeof (struct channel_info));
        channelInfoArray[i] = thisChannelinfo;
        char *channelName = currentChannel->_this->channelName;
        memcpy(thisChannelinfo->ch_channel, channelName, CHANNEL_MAX);

        // Find the next channel.
        currentChannel = currentChannel->_next;
    }

    // Creates the channel list datagram.
    int datagramSize = sizeof(struct text_list) + (sizeof(struct channel_info) * (channelCount));
    struct text_list *datagram = (struct text_list *)malloc(datagramSize);
    memset(datagram, 0, datagramSize);

    // Set the properties of the datagram.
    datagram->txt_type = TXT_LIST;
    datagram->txt_nchannels = channelCount;
    for (int i = 0; i < channelCount; i++)
        memcpy(
            (datagram + ((8 + (sizeof(struct channel_info) * i)) / 8)),
            &((channelInfoArray[i])->ch_channel),
            sizeof(struct channel_info)
        );

    // Free all the memory.
    for (int i = 0; i < channelCount; i++)
        free(channelInfoArray[i]);
    free(channelInfoArray);
    return (void *)datagram;
}

void *make_who_datagram(struct UserRef *channelUsers, struct Channel *channel) {
    // Count users in channel.
    int userCount = 0;
    struct UserRef *currentCountPos = channelUsers;
    while (currentCountPos != NULL) {
        userCount += 1;
        currentCountPos = currentCountPos->_next;
    }

    // Figure out our channel data.
    struct user_info **userInfoArray = (struct user_info **)malloc(sizeof(struct user_info*) * userCount);
    struct UserRef *currentUser = channelUsers;
    for (int i = 0; i < userCount; i++) {
        // Make a user info here.
        struct user_info *thisUserInfo = (struct user_info *)malloc(sizeof (struct user_info));
        userInfoArray[i] = thisUserInfo;
        char *username = currentUser->_this->username;
        memcpy(thisUserInfo->us_username, username, USERNAME_MAX);

        // Find the next user.
        currentUser = currentUser->_next;
    }

    // Creates the who datagram.
    int datagramSize = sizeof(struct text_who) + (sizeof(struct user_info) * (userCount));
    struct text_who *datagram = (struct text_who *)malloc(datagramSize);
    memset(datagram, 0, datagramSize);

    // Set the properties of the datagram.
    datagram->txt_type = TXT_WHO;
    datagram->txt_nusernames = userCount;
    memcpy(datagram->txt_channel, channel->channelName, CHANNEL_MAX);
    for (int i = 0; i < userCount; i++)
        memcpy(
            (((char *)datagram) + (40 + (sizeof(struct user_info) * i))),
            &((userInfoArray[i])->us_username),
            sizeof(struct user_info)
        );

    // Free all the memory.
    for (int i = 0; i < userCount; i++)
        free(userInfoArray[i]);
    free(userInfoArray);
    return (void *)datagram;
}

void *make_error_datagram(const char *text) {
    struct text_error *datagram = (struct text_error *)malloc(sizeof(text_error));
    memset(datagram, 0, sizeof(text_error));
    datagram->txt_type = TXT_ERROR;
    memcpy(datagram->txt_error, text, SAY_MAX);
    return (void *)datagram;
}

int get_error_datagram_size() {
    return sizeof(text_error);
}

#endif
