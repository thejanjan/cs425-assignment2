#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <cerrno>

#include "duckchat.h"
#include "raw.h"
#include "server.h"
#include "utils.h"
#include "topology.h"

static const Topology *topology = NULL;
static struct sockaddr_in *serverAddress = NULL;

/*
 * Establishing Connection
 */

int create_socket() {
    return socket(
        AF_INET,     // Domain
        SOCK_DGRAM,  // Type
        IPPROTO_UDP  // UDP Protocol (defined in /etc/protocols)
    );
}

void perform_heartbeat(int) {
    /* Performs the user heartbeat check. */
    struct AddressRef *addressList = create_address_list(NULL);
    struct UserRef *userRef = userList;

    // Get the address of every expired user.
    while (userRef != NULL) {
        if (userRef->_this == NULL) break;
        if (has_user_expired(userRef->_this))
            add_address_to_list(addressList, userRef->_this->address);
        userRef = userRef->_next;
    }

    // Remove ALL OF THEM that have expired.
    while (addressList != NULL) {
        if (addressList->_this == NULL) break;
        printf("Removing a user (failed to respond to heartbeat)\n");
        remove_user(*(addressList->_this));
        addressList = addressList->_next;
    }
    fflush(stdout);

    // Cleanup.
    free_address_list(addressList);

    // Prepare the next keepalive call.
    alarm(SERVER_KEEPALIVE);
}

void topology_renew(int) {
    // Renew the topology.
    topology->renew(topology, serverAddress, channelList);

    // Prepare the next topology renew call.
    alarm(TOPOLOGY_RENEW);
}

void event_loop(const int openSocket, struct sockaddr_in *serverAddr) {
    // Notify
    printf("Initializing event loop...\n");
    printf("Press enter to terminate process.\n");
    fflush(stdout);

    // Get some consts defined.
    const int fds_cnt = 2;          // file descriptors
    const int poll_timeout = 0;    // poll timeout

    // Various setup
    char *buffer = (char *)malloc(sizeof(char) * (BUFFER_SIZE + 1));
    struct sockaddr_in *address = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    int result;

    // Set up two file descriptors to poll for:
    // our socket value and standard input.
    struct pollfd fds[fds_cnt] = {
        {fd: openSocket, events: POLLIN, revents: 0},
        {fd: STDIN_FILENO, events: POLLIN, revents: 0}
    };

    // Prepare keep-alive.
    signal(SIGALRM, topology_renew);
    alarm(TOPOLOGY_RENEW);

    // Start the poll loop.
    while (1) {
        // Clear out our buffer and address.
        memset(buffer, 0, BUFFER_SIZE);
        memset(address, 0, sizeof(struct sockaddr_in));

        // Perform polling.
        poll(fds, fds_cnt, poll_timeout);

        // Determine what has updated.
        if (fds[0].revents & POLLIN) {
            // We have received a message from a client.
            socklen_t addressLength = sizeof(address);
            result = recvfrom(
                openSocket, buffer, BUFFER_SIZE, 0,
                (struct sockaddr *)address, &addressLength
            );
            if (result < 0) {
                printf("An error occured while receiving a message.\n");
            } else {
                // What request type are we dealing with?
                request_t *requestType = (request_t *)malloc(sizeof(request_t));
                if (requestType == NULL) {fprintf(stderr, "Out of memory"); break;}
                memcpy((void *)requestType, (const void *)buffer, sizeof(request_t));

                // Set keepalive.
                User *user = get_user(*address);
                if (user != NULL)
                    heartbeat_user(user);

                // Prepare a datagram callback.
                void *response = NULL;
                int response_size = 0;
                bool send = false;
                struct AddressRef *addressList = create_address_list(NULL);

                // Nice shorthand
                #define error_datagram(msg) response = make_error_datagram(msg); response_size = get_error_datagram_size(); send = true; add_address_to_list(addressList, address)

                // Handle the request types differently.
                switch (*requestType) {
                    //            //
                    // USER LOGIN //
                    //            //
                    case REQ_LOGIN: {
                        // Decipher the request.
                        request_login *datagram = (request_login *)malloc(sizeof(request_login));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_login));

                        // Add the user.
                        User *existing_user = get_user(*address);
                        if (existing_user != NULL) {
                            // they have an address still linked -- log them out and cleanup first
                            print_addresses(serverAddr, address);
                            printf("recv Request Logout %s\n", existing_user->username);
                            remove_user(*address);
                        }
                        bool result = create_user(*address, datagram->req_username);
                        if (result) {
                            User *user = get_user(*address);
                            if (user == NULL) {
                                printf("User logged on, but user creation FAILED!\n");
                                error_datagram("Login failure.");
                            } else {
                                // printf("User logged on. Username: %s\n", user->username);
                                print_addresses(serverAddr, address);
                                printf("recv Request Login %s\n", user->username);

                                // In our implementation, we force add the client to Common.
                                bool isNew = ((get_initial_channel()->userCount) == 0);
                                add_user_to_channel(user, get_initial_channel());
                                print_addresses(serverAddr, address);
                                printf("recv Request Join %s Common\n", user->username);

                                // Send call to topology -- only if this channel is "new".
                                if (isNew)
                                    topology->s2s_join_send(topology, serverAddr, address, get_initial_channel()->channelName);
                            }
                        } else {
                            printf("User logged on, but user creation failed!");
                            error_datagram("Login failure.");
                        }

                        // Cleanup.
                        free(datagram);
                        } break;

                    //             //
                    // USER LOGOUT //
                    //             //
                    case REQ_LOGOUT: {
                        // Decipher the request.
                        request_logout *datagram = (request_logout *)malloc(sizeof(request_logout));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_logout));

                        User *user = get_user(*address);
                        if (user != NULL) {
                            print_addresses(serverAddr, address);
                            printf("recv Request Logout %s\n", user->username);
                        }

                        // Clean up the user.
                        remove_user(*address);

                        // Cleanup.
                        free(datagram);
                        } break;

                    //                   //
                    // USER JOIN CHANNEL //
                    //                   //
                    case REQ_JOIN: {
                        // Decipher the request.
                        request_join *datagram = (request_join *)malloc(sizeof(request_join));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_join));

                        // Get the user.
                        User *user = get_user(*address);
                        if (user != NULL) {
                            // Get the channel.
                            struct Channel *channel = get_channel(datagram->req_channel, true);

                            // Make sure they aren't in this channel.
                            if (is_user_in_channel(user, channel)) {
                                // The user is already in here, do nothing.
                                printf("User %s tried to join a channel they were already in.\n", user->username);
                                error_datagram("You are already in this channel.");
                            } else {
                                // Add them to the channel.
                                bool isNew = ((channel->userCount) == 0);
                                add_user_to_channel(user, channel);
                                print_addresses(serverAddr, address);
                                printf("recv Request Join %s %s\n", user->username, channel->channelName);
                                // printf("User %s joined channel %s.\n", user->username, channel->channelName);

                                char *callback = (char *)malloc(sizeof(char) * SAY_MAX);
                                sprintf(callback, "Joined channel [%s].", channel->channelName);
                                error_datagram(callback);
                                free(callback);

                                // Send call to topology.
                                if (isNew)
                                    topology->s2s_join_send(topology, serverAddr, address, datagram->req_channel);
                            }
                        } else {
                            printf("User tried to join a channel, but the User did not exist.\n");
                            error_datagram("You are not logged in. Please restart the client.");
                        }
                        fflush(stdout);

                        // Cleanup.
                        free(datagram);
                        } break;
                    
                    //                    //
                    // USER LEAVE CHANNEL //
                    //                    //
                    case REQ_LEAVE: {
                        // Decipher the request.
                        request_leave *datagram = (request_leave *)malloc(sizeof(request_leave));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_leave));

                        // Get the user.
                        User *user = get_user(*address);
                        if (user != NULL) {
                            // Get the channel.
                            struct Channel *channel = get_channel(datagram->req_channel, false);

                            // Make sure they are in this channel.
                            if (channel == NULL) {
                                printf("User %s tried to leave a channel that does not exist.\n", user->username);
                                error_datagram("You cannot leave a channel that doesn't exist.");
                            } else if (!is_user_in_channel(user, channel)) {
                                // The user is not here, do nothing.
                                printf("User %s tried to leave a channel they were not already in.\n", user->username);
                                error_datagram("You cannot leave a channel you are not in.");
                            } else {
                                // Don't let them leave common.
                                // if (channel == get_initial_channel()) {
                                //     printf("User %s tried to leave Common. Prevented.\n", user->username);
                                //     error_datagram("You cannot leave Common!");
                                // } else {
                                    // Remove them from the channel.
                                    print_addresses(serverAddr, address);
                                    printf("recv Request Leave %s\n", channel->channelName);
                                    //printf("User %s left channel %s.\n", user->username, channel->channelName);

                                    char *callback = (char *)malloc(sizeof(char) * SAY_MAX);
                                    sprintf(callback, "Left channel [%s].", channel->channelName);
                                    error_datagram(callback);
                                    free(callback);

                                    remove_user_from_channel(user, channel);
                                // }
                            }
                        } else {
                            printf("User tried to leave a channel, but the User did not exist.\n");
                            error_datagram("You are not logged in. Please restart the client.");
                        }
                        fflush(stdout);

                        // Cleanup.
                        free(datagram);
                        } break;

                    //                   //
                    // USER SAYS MESSAGE //
                    //                   //
                    case REQ_SAY: {
                        // Find the user.
                        User *user = get_user(*address);
                        if (user == NULL) {
                            printf("Say request received, but user not found.\n");
                            error_datagram("You are not logged in. Please restart the client.");
                            break;
                        }

                        // Decipher the request.
                        request_say *datagram = (request_say *)malloc(sizeof(request_say));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_say));

                        // Format and say the message server-side.
                        scrub_channel_name(datagram->req_channel);
                        scrub_chat_msg(datagram->req_text);

                        print_addresses(serverAddr, address);
                        printf("recv Request Say %s \"%s\"\n", datagram->req_channel, datagram->req_text);
                        // printf("[%s][%s]: %s\n", datagram->req_channel, user->username, datagram->req_text);

                        // Make our datagram.
                        text_say *say_response = (text_say *)malloc(sizeof(text_say));
                        if (say_response == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memset(say_response, '\0', sizeof(text_say));

                        // Popualte the datagram.
                        say_response->txt_type = TXT_SAY;
                        memcpy(say_response->txt_channel, datagram->req_channel, CHANNEL_MAX);
                        memcpy(say_response->txt_username, user->username, USERNAME_MAX);
                        memcpy(say_response->txt_text, datagram->req_text, SAY_MAX);

                        // Package it up
                        response = (void *)say_response;
                        response_size = sizeof(request_say);
                        send = true;

                        // This will be sent out to all channels.
                        struct Channel *channel = get_channel(datagram->req_channel, false);
                        if (channel != NULL) {
                            struct UserRef *channelUsers = get_users_in_channel(channel);
                            if (channelUsers != NULL) {
                                struct UserRef *startList = channelUsers;
                                // Iterate over all users and add them to the list.
                                while (channelUsers != NULL) {
                                    // Add their address.
                                    add_address_to_list(addressList, channelUsers->_this->address);
                                    // Next one
                                    channelUsers = channelUsers->_next;
                                }
                                // Cleanup.
                                free_user_ref(startList);
                            }
                        } else {
                            printf("User %s tried to send a message into a non-existent channel.\n", user->username);
                            error_datagram("Channel does not exist.");
                        }
                        fflush(stdout);

                        // Send call to topology.
                        topology->s2s_say_send(topology, serverAddr, NULL,
                                               user->username, datagram->req_channel,
                                               datagram->req_text, 0);

                        // Cleanup.
                        free(datagram);
                        } break;

                    //                            //
                    // USER REQUESTS CHANNEL LIST //
                    //                            //
                    case REQ_LIST: {
                        // Find the user.
                        User *user = get_user(*address);
                        if (user == NULL) {
                            printf("List request received, but user not found.\n");
                            error_datagram("You are not logged in. Please restart the client.");
                            break;
                        }
                        // printf("User %s requested channel listing.\n", user->username);
                        print_addresses(serverAddr, address);
                        printf("recv Request List %s\n", user->username);

                        // Make our datagram.
                        response = make_channel_list_datagram();
                        response_size = get_channel_list_datagram_size(response);
                        send = true;
                        add_address_to_list(addressList, address);
                        } break;

                    //                         //
                    // USER REQUESTS USER LIST //
                    //                         //
                    case REQ_WHO: {
                        // Find the user.
                        User *user = get_user(*address);
                        if (user == NULL) {
                            printf("Say request received, but user not found.\n");
                            error_datagram("You are not logged in. Please restart the client.");
                            break;
                        }

                        // Decipher the request.
                        request_who *datagram = (request_who *)malloc(sizeof(request_who));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_who));

                        print_addresses(serverAddr, address);
                        printf("recv Request Who %s %s\n", user->username, datagram->req_channel);

                        // Get the channel.
                        struct Channel *channel = get_channel(datagram->req_channel, false);
                        if (channel != NULL) {
                            // Get the users in the channel.
                            struct UserRef *channelUsers = get_users_in_channel(channel);
                            if (channelUsers != NULL) {
                                // It exists! We can make our response.
                                response = make_who_datagram(channelUsers, channel);
                                response_size = get_who_datagram_size(response);
                                send = true;
                                add_address_to_list(addressList, address);

                                // Cleanup.
                                free_user_ref(channelUsers);
                            }
                        } else {
                            printf("User %s tried to get user list info from a nonexistent channel.\n", user->username);
                            
                            char *callback = (char *)malloc(sizeof(char) * SAY_MAX);
                            sprintf(callback, "Channel [%s] does not exist.", datagram->req_channel);
                            error_datagram(callback);
                            free(callback);
                        }

                        // Cleanup.
                        free(datagram);
                        } break;

                    //                       //
                    // KEEP ALIVE MANAGEMENT //
                    //                       //
                    case REQ_KEEP_ALIVE: break;  // Receiving this call forces a keepalive anyways

                    //          //
                    // S2S JOIN //
                    //          //
                    case S2S_JOIN: {
                        // Decipher the request.
                        request_server_join *datagram = (request_server_join *)malloc(sizeof(request_server_join));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_server_join));

                        // Defer action to topology.
                        topology->s2s_join_recv(topology, serverAddr, &(datagram->address), datagram->req_channel);

                        // Cleanup.
                        free(datagram);
                        break;
                    }
                    //           //
                    // S2S LEAVE //
                    //           //
                    case S2S_LEAVE: {
                        // Decipher the request.
                        request_server_leave *datagram = (request_server_leave *)malloc(sizeof(request_server_leave));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_server_leave));

                        // Defer action to topology.
                        topology->s2s_leave_recv(topology, serverAddr, &(datagram->address), datagram->req_channel);

                        // Cleanup.
                        free(datagram);
                        break;
                    }
                    //         //
                    // S2S SAY //
                    //         //
                    case S2S_SAY: {
                        // Decipher the request.
                        request_server_say *datagram = (request_server_say *)malloc(sizeof(request_server_say));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(request_server_say));

                        // Defer action to topology.
                        bool success = topology->s2s_say_recv(topology, serverAddr, &(datagram->address),
                                               datagram->id, datagram->txt_username, 
                                               datagram->txt_channel, datagram->txt_text);

                        // If this message was new, send it out to all users.
                        if (success) {
                            // Make our datagram.
                            text_say *say_response = (text_say *)malloc(sizeof(text_say));
                            if (say_response == NULL) {fprintf(stderr, "Out of memory"); break;}
                            memset(say_response, '\0', sizeof(text_say));
    
                            // Popualte the datagram.
                            say_response->txt_type = TXT_SAY;
                            memcpy(say_response->txt_channel, datagram->txt_channel, CHANNEL_MAX);
                            memcpy(say_response->txt_username, datagram->txt_username, USERNAME_MAX);
                            memcpy(say_response->txt_text, datagram->txt_text, SAY_MAX);
    
                            // Package it up
                            response = (void *)say_response;
                            response_size = sizeof(request_say);
                            send = true;
    
                            // This will be sent out to all channels.
                            struct Channel *channel = get_channel(datagram->txt_channel, false);
                            if (channel != NULL) {
                                struct UserRef *channelUsers = get_users_in_channel(channel);
                                if (channelUsers != NULL) {
                                    struct UserRef *startList = channelUsers;
                                    // Iterate over all users and add them to the list.
                                    while (channelUsers != NULL) {
                                        // Add their address.
                                        add_address_to_list(addressList, channelUsers->_this->address);
                                        // Next one
                                        channelUsers = channelUsers->_next;
                                    }
                                    // Cleanup.
                                    free_user_ref(startList);
                                }
                            } else {
                                printf("User %s tried to send a message into a non-existent channel.\n", datagram->txt_username);
                                error_datagram("Channel does not exist.");
                            }
                            fflush(stdout);
                        }

                        // Cleanup.
                        free(datagram);
                        break;
                    }
                    //                       //
                    // COOL AND AWESOME HACK //
                    //                       //
                    case REQ_BAD:
                        // Ignore this request :P
                        break;

                    //                 //
                    // UNKNOWN REQUEST //
                    //                 //
                    default:
                        printf("Received undefined request, ignoring\n");
                        break;
                }
                // See if we're sending something back to clients.
                if (send) {
                    // Send a response back to all users in the address list.
                    while (addressList->_this != NULL) {
                        // Send the datagram to this address.
                        struct sockaddr_in *address = addressList->_this;
                        int result = sendto(
                            openSocket, response, response_size, MSG_DONTWAIT,
                            (struct sockaddr *)address, sizeof(struct sockaddr_in)
                        );
                        if (result == -1) {
                            // Something went wrong with sending this.
                            fprintf(stderr, "Response to client failed to send. (%d)\n", result);
                        }

                        // Move to the next address.
                        if (addressList->_next == NULL) break;
                        addressList = addressList->_next;
                    }
                }

                // Cleanup.
                free_address_list(addressList);
                free(requestType);
                if (response != NULL) free(response);
            }
        }
        if (fds[1].revents & POLLIN) {
            // If we get user input, break the loop if we have no topology.
            if (topology->get_size(topology) == 0) break;
        }
    }

    // Post-loop cleanup.
    free((void *)buffer);
    free((void *)address);
}

/*
 *  Main
 */

int main(int argc, char *argv[]) {
    // Validate arguments.
    if ((argc < 3) || !(argc % 2)) {
        fprintf(stderr, "Usage: %s <hostname> <port> optional: <hostnameA> <portA>, <hostnameB> <portB>, etc\n", argv[0]);
        exit(1);
    }
    char *hostname = argv[1];
    char *port = argv[2];

    // Create the socket.
    int openSocket = create_socket();
    if (openSocket < 0) {
        fprintf(stderr, "Could not open socket\n");
        exit(1);
    }

    // After creating the socket, put together our address.
    struct hostent *he;
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(port));
    if ((he = gethostbyname(hostname)) == NULL) {
        fprintf(stderr, "Error resolving hostname\n");
        exit(1);
    }
    memcpy(&serverAddr.sin_addr, he->h_addr_list[0], he->h_length);
    serverAddress = &serverAddr;

    // Bind the address to the socket.
    int result = bind(openSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    if (result < 0) {
        // Bind failure.
        printf("%d %s\n", errno, strerror(errno));
        fprintf(stderr, "Socket bind failed.\n");
        close(openSocket);
        exit(1);
    }

    // Bind the server topology.
    topology = Topology_create();
    for (int i = 3; i < (argc - 1); i += 2) {
        // Get our arguments.
        char *server_hostname = argv[i];
        char *server_port = argv[i + 1];

        // Attempt to open socket.
        int serverSocket = create_socket();
        if (serverSocket < 0) {
            // Bind failure.
            fprintf(stderr, "Topolgy socket make failed.\n");
            topology->cleanup(topology);
            close(openSocket);
            close(serverSocket);
            exit(1);
        }

        // Attempt to add address.
        bool success = topology->add_address(topology, serverSocket, server_hostname, server_port);
        if (!success) {
            // Bind failure.
            topology->cleanup(topology);
            close(openSocket);
            exit(1);
        }
    }

    // Various initialization.
    initialize_channels();
    initialize_users();

    // Begin the event loop.
    event_loop(openSocket, &serverAddr);

    // Cleanup.
    printf("Cleaning up channels...\n");
    cleanup_channels();
    printf("Cleaning up users...\n");
    cleanup_users();
    printf("Cleaning up socket...\n");
    close(openSocket);
    printf("Cleaning up topology...\n");
    topology->cleanup(topology);
    printf("Goodbye!\n");
    return 0;
}
