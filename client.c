#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "client.h"
#include "duckchat.h"
#include "raw.h"
#include "utils.h"

/*
 * Message Management
 */

// strcmp(buffer, "\n") == 0
#define is_command(buffer) (buffer[0] == '/')

bool test_command(char *buffer, const char *cmd) {
    bool cmd_match = (strstr(buffer, cmd) == (buffer + 1));
    if (!cmd_match) return false;

    // Make sure the command terminates.
    int cmd_len = strlen(cmd);
    char end_char = *(((buffer) + 1) + cmd_len);
    if (end_char != '\0')
        if (end_char != ' ')
            if (end_char != '\n')
                return false;
    return true;
}

char *get_command_argument(char *buffer) {
    // Gets the inline command argument.
    // Returns a null string if no argument is present.
    char *arg = buffer;
    while (1) {
        arg++;
        if ((*arg) == ' ') return (arg + 1);
        if ((*arg) == '\0') return arg;
        if ((*arg) == '\n') return arg;
    }
}


void prepare_input() {
    /* Prepares a command terminal on-screen. */
    printf("> ");
    fflush(stdout);
}

void good_morning() {
    // Gives the good morning and motd!
    // printf("        ,----,\n");
    // printf("   ___.`      `,\n");
    // printf("   `===  D     :   <- he is crying because he loves\n");
    // printf("     `'. \\    .'      to chat with his friends on DuckChat\n");
    // printf("        ) \\  (                   ,\n");
    // printf("       / \\   \\_________________/|\n");
    // printf("      /   \\                     |\n");
    // printf("     |                           ;\n");
    // printf("     |               _____       /\n");
    // printf("     |      \\       ______7    ,'\n");
    // printf("     |       \\    ______7     /\n");
    // printf("      \\       `-,____7      ,'\n");
    // printf("^~^~^~^`\\                  /~^~^~^~^\n");
    // printf("  ~^~^~^ `----------------' ~^~^~^\n");
    // printf(" ~^~^~^~^~^^~^~^~^~^~^~^~^~^~^~^~\n\n");
    // printf("    -=- WELCOME TO DUCKCHAT -=-\n");
    // printf("      Where Dreams Come Alive\n");
    // printf("           (circa @1996)\n\n");
    printf("You are currently in the [Common] channel.\n");
    printf("Use /help for a list of commands.\n");
}


/*
 * Establishing Connection
 */

// A small container struct for passing back socket data.
struct socket_data {
    int socketFd;
    sockaddr_in address;
};

static socket_data socketData;

bool create_socket(char *node, char *service) {
    // Figure out what our server address is.
    const struct addrinfo hints = {
        0, AF_INET, SOCK_DGRAM, IPPROTO_UDP, 0, 0, 0, 0
    };
    struct addrinfo *res;
    int result = getaddrinfo(node, service, &hints, &res);
    if (result != 0) {
        fprintf(stderr, "Error when getting address info.");
        return false;
    }

    // Attempt to make a socket at each address.
    void *resStart = (void *)res;
    int socketFd;
    while (res != NULL) {
        // Open a socket.
        socketFd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        // Break if the socket opens.
        if (socketFd >= 0) break;

        // No good, check next address.
        res = res->ai_next;
    }
    if (res == NULL) {
        // Address info could not be found.
        fprintf(stderr, "No valid socket address could be found.");
        res = (addrinfo *)resStart;
        freeaddrinfo(res);
        return false;
    }

    // Bind it to socket data.
    res = (addrinfo *)resStart;
    socketData.socketFd = socketFd;
    memset(&(socketData.address), 0, sizeof(socketData.address));
    memcpy(&(socketData.address), res->ai_addr, res->ai_addrlen);

    // Everything is good to go! Cleanup.
    freeaddrinfo(res);
    return true;
}

bool login(char *username) {
    /*
     * Sends a login packet.
     */
    // Put the packet together.
    request_login *datagram = (request_login *)malloc(sizeof(request_login));
    if (datagram == NULL) {fprintf(stderr, "Out of memory"); return false;}
    memset((void *)datagram, 0, sizeof(request_login));
    datagram->req_type = REQ_LOGIN;
    strncat(datagram->req_username, username, USERNAME_MAX);

    // HACK: Sometimes, the server receives the first address incorrectly.
    //       Soooo we are going to make the first request bad, and let the server ignore.
    //       I couldn't find anything about this >_<
    request_t bad_request = REQ_BAD;
    sendto(
        socketData.socketFd,
        (void *)(&bad_request), 1, 0,
        (const struct sockaddr *)(&socketData.address),
        sizeof(struct sockaddr_in)
    );

    // Then send an actual login request.
    int result = sendto(
        socketData.socketFd,
        (void *)datagram, sizeof(request_login), 0,
        (const struct sockaddr *)(&socketData.address),
        sizeof(struct sockaddr_in)
    );
    free((void *)datagram);
    if (result < 0) return false;
    return true;
}

void keep_alive(int) {
    // Send a keepalive data packet.
    request_keep_alive datagram = {req_type: REQ_KEEP_ALIVE};
    int message_size = sizeof(datagram);
    sendto(
        socketData.socketFd,
        (void *)(&datagram), message_size, 0,
        (const struct sockaddr *)(&(socketData.address)),
        sizeof(struct sockaddr_in)
    );

    // Prepare the next keepalive call.
    alarm(CLIENT_KEEPALIVE);
}

void event_loop() {
    // Performs the event loop.
    const int fds_cnt = 2;          // file descriptors
    const int poll_timeout = 0;    // poll timeout

    // Various setup
    char buffer[BUFFER_SIZE + 1];
    int result;
    prepare_input();

    // Decompose our socket data.
    int openSocket = socketData.socketFd;
    sockaddr_in address = socketData.address;

    // Set up two file descriptors to poll for:
    // our socket value and standard input.
    struct pollfd fds[fds_cnt] = {
        {fd: STDIN_FILENO, events: POLLIN, revents: 0},
        {fd: openSocket, events: POLLIN, revents: 0}
    };

    // Prepare keep-alive.
    // signal(SIGALRM, keep_alive);
    // alarm(CLIENT_KEEPALIVE);

    // Start the poll loop.
    while (1) {
        // Perform polling.
        poll(fds, fds_cnt, poll_timeout);
        // Determine what has updated.
        if (fds[0].revents & POLLIN) {
            // We got something from user input, kinda Epic.
            fgets(buffer, BUFFER_SIZE, stdin);

            // Some locals for our command.
            void *raw_datagram;
            size_t message_size;
            bool send = false;
            bool leave = false;

            // Decipher our message.
            if (!is_command(buffer)) {
                // This is being sent out as a regular message.
                request_say *datagram = (request_say *)malloc(sizeof(request_say));
                if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                memset((void *)datagram, 0, sizeof(request_say));
                datagram->req_type = REQ_SAY;
                strncat(datagram->req_channel, current_channel, CHANNEL_MAX);
                strncat(datagram->req_text, buffer, SAY_MAX);

                // We will be sending this out.
                raw_datagram = (void *)datagram;
                message_size = sizeof(request_say);
                send = true;
            } else {
                // Figure out which command this is.
                if (test_command(buffer, "exit")) {
                    // Leave the chat.
                    printf("Goodbye!\n");

                    // Format datagram.
                    request_logout *datagram = (request_logout *)malloc(sizeof(request_logout));
                    memset((void *)datagram, 0, sizeof(request_logout));
                    datagram->req_type = REQ_LOGOUT;

                    // We will be sending this out.
                    raw_datagram = (void *)datagram;
                    message_size = sizeof(request_leave);
                    send = true;

                    // Set the leave flag.
                    leave = true;
                } else if (test_command(buffer, "help")) {
                    // Print help.
                    printf("-=- Command List -=-\n");
                    printf("/join <channel>   - Joins/creates a specified channel.\n");
                    printf("/leave <channel>  - Leaves a specified channel.\n");
                    printf("/list             - Lists the name of all channels.\n");
                    printf("/who <channel>    - Lists the users on the given channel.\n");
                    printf("/switch <channel> - Switches to an existing named channel that has been joined.\n");
                    printf("/exit             - Exits DuckChat.\n");
                } else if (test_command(buffer, "join")) {
                    // Ensure the argument is valid.
                    char *arg = get_command_argument(buffer);
                    if (arg == NULL) {
                        printf("Usage: /join <channel>\n");
                    } else {
                        // Format datagram.
                        request_join *datagram = (request_join *)malloc(sizeof(request_join));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memset((void *)datagram, 0, sizeof(request_join));
                        datagram->req_type = REQ_JOIN;
                        strcpy(current_channel, arg);
                        strncat(datagram->req_channel, arg, CHANNEL_MAX);

                        scrub_channel_name(arg);
                        if (!is_channel_name_real(arg))
                            add_channel(arg);

                        // We will be sending this out.
                        raw_datagram = (void *)datagram;
                        message_size = sizeof(request_join);
                        send = true;
                    }
                } else if (test_command(buffer, "leave")) {
                    // Ensure the argument is valid.
                    char *arg = get_command_argument(buffer);
                    if (arg == NULL) {
                        printf("Usage: /leave <channel>\n");
                    } else {
                        // Format datagram.
                        request_leave *datagram = (request_leave *)malloc(sizeof(request_leave));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memset((void *)datagram, 0, sizeof(request_leave));
                        datagram->req_type = REQ_LEAVE;
                        strncat(datagram->req_channel, arg, CHANNEL_MAX);

                        scrub_channel_name(arg);
                        remove_channel(arg);

                        // We will be sending this out.
                        raw_datagram = (void *)datagram;
                        message_size = sizeof(request_leave);
                        send = true;
                    }
                } else if (test_command(buffer, "list")) {
                    // Format datagram.
                    request_list *datagram = (request_list *)malloc(sizeof(request_list));
                    if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                    memset((void *)datagram, 0, sizeof(request_list));
                    datagram->req_type = REQ_LIST;

                    // We will be sending this out.
                    raw_datagram = (void *)datagram;
                    message_size = sizeof(request_list);
                    send = true;
                } else if (test_command(buffer, "who")) {
                    // Ensure the argument is valid.
                    char *arg = get_command_argument(buffer);
                    if (arg == NULL) {
                        printf("Usage: /who <channel>\n");
                    } else {
                        // Format datagram.
                        request_who *datagram = (request_who *)malloc(sizeof(request_who));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memset((void *)datagram, 0, sizeof(request_who));
                        datagram->req_type = REQ_WHO;
                        strncat(datagram->req_channel, arg, CHANNEL_MAX);

                        // We will be sending this out.
                        raw_datagram = (void *)datagram;
                        message_size = sizeof(request_who);
                        send = true;
                    }
                } else if (test_command(buffer, "switch")) {
                    // Ensure the argument is valid.
                    char *arg = get_command_argument(buffer);
                    if (arg == NULL) {
                        printf("Usage: /switch <channel>\n");
                    } else {
                        // Does the channel exist?
                        scrub_channel_name(arg);
                        if (is_channel_name_real(arg)) {
                            if (strcmp(current_channel, arg) == 0)
                                printf("You are already in channel [%s].\n", arg);
                            else {
                                strcpy(current_channel, arg);
                                printf("Switched to channel [%s].\n", arg);
                            }
                        } else
                            printf("You are not in [%s], so you may not switch to it.\n", arg);
                    }
                } else {
                    // Invalid command given.
                    printf("Invalid command. Use /help for a list of commands.\n");
                }
            }

            // Send our datagram.
            if (send) {
                result = sendto(
                    openSocket,
                    (void *)raw_datagram, message_size, 0,
                    (const struct sockaddr *)(&address),
                    sizeof(struct sockaddr_in)
                );
                free(raw_datagram);
                if (result < 0) {
                    fprintf(stderr, "Message failed to transmit. Disconnecting...\n");
                    break;
                }
                // Reset keepalive.
                alarm(CLIENT_KEEPALIVE);
            }

            // Leave if necessary.
            if (leave)
                break;

            // Prepare input now for the next message.
            prepare_input();
        }
        if (fds[1].revents & POLLIN) {
            // The server has sent us something.
            int result = recv(openSocket, buffer, BUFFER_SIZE, 0);
            if (result < 0) {
                // We received a bad call. Let's close the connection
                printf("Connection terminated.\n");
                break;
            } else {
                // We received a message. First, undo the current client input.
                for (int i = 0; i < 512; i++)
                    printf("\b");
                fflush(stdout);

                // Now, figure out the datagram that we received.
                // What request type are we dealing with?
                text_t *textType = (text_t *)malloc(sizeof(text_t));
                if (textType == NULL) {fprintf(stderr, "Out of memory"); break;}
                memcpy((void *)textType, (const void *)buffer, sizeof(request_t));

                // Handle the request types differently.
                switch (*textType) {
                    //                                        //
                    // PERSON SAYS AWESOME IMPORTANT MESSAGES //
                    //                                        //
                    case TXT_SAY: {
                        // Decipher the request.
                        text_say *datagram = (text_say *)malloc(sizeof(text_say));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(text_say));

                        // Print the message.
                        scrub_channel_name(datagram->txt_channel);
                        printf("[%s][%s]: %s\n", datagram->txt_channel, datagram->txt_username, datagram->txt_text);

                        // Cleanup.
                        fflush(stdout);
                        free(datagram);
                        } break;

                    //              //
                    // CHANNEL LIST //
                    //              //
                    case TXT_LIST: {
                        // Decipher the request.
                        text_list *datagram = (text_list *)malloc(sizeof(text_list));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(text_list));

                        // Re-allocate with real size.
                        int datagram_size = get_channel_list_datagram_size((void *)datagram);
                        datagram = (text_list *)realloc((void *)datagram, datagram_size);
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, datagram_size);

                        // Print the channel listing.
                        printf("Existing channels:\n");
                        for (int i = 0; i < datagram->txt_nchannels; i++) {
                            // TODO - validate we aren't receiving bad data
                            struct channel_info channelInfo = (datagram->txt_channels)[i];
                            printf("  %s\n", channelInfo.ch_channel);
                        }

                        // Cleanup.
                        fflush(stdout);
                        free(datagram);
                        } break;

                    //                //
                    // WHO IN CHANNEL //
                    //                //
                    case TXT_WHO: {
                        // Decipher the request.
                        text_who *datagram = (text_who *)malloc(sizeof(text_who));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(text_who));

                        // Re-allocate with real size.
                        int datagram_size = get_who_datagram_size((void *)datagram);
                        datagram = (text_who *)realloc((void *)datagram, datagram_size);
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, datagram_size);

                        // Print the channel listing.
                        printf("Users on channel %s:\n", datagram->txt_channel);
                        for (int i = 0; i < datagram->txt_nusernames; i++) {
                            // TODO - validate we aren't receiving bad data
                            struct user_info userInfo = (datagram->txt_users)[i];
                            printf("  %s\n", userInfo.us_username);
                        }

                        // Cleanup.
                        fflush(stdout);
                        free(datagram);
                        } break;

                    //                //
                    // ERROR CALLBACK //
                    //                //
                    case TXT_ERROR: {
                        // Decipher the request.
                        text_error *datagram = (text_error *)malloc(sizeof(text_error));
                        if (datagram == NULL) {fprintf(stderr, "Out of memory"); break;}
                        memcpy((void *)datagram, (const void *)buffer, sizeof(text_error));

                        // Print the message.
                        printf("%s\n", datagram->txt_error);

                        // Cleanup.
                        fflush(stdout);
                        free(datagram);
                        } break;

                    //                 //
                    // UNKNOWN REQUEST //
                    //                 //
                    default:
                        printf("Received undefined request, ignoring\n");
                        break;
                }

                // Bring back the client input.
                fflush(stdout);
                prepare_input();
            }
        }
    }
}

/*
 * Main
 */

int main(int argc, char *argv[]) {
    // Validate arguments
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <hostname> <port> <username>\n", argv[0]);
        exit(0);
    }

    // Get our arguments.
    char *hostname = argv[1];
    char *port = argv[2];

    // Open the socket.
    bool result = create_socket(hostname, port);
    if (result == false) {
        fprintf(stderr, "Socket initialization failed.\n");
        return 0;
    }

    // Do some initiation.
    login(argv[3]);

    // TODO move the below out of here
    add_channel("Common");
    good_morning();

    // Perform event loop.
    event_loop();

    // Cleanup.
    cleanup_channels();
    close(socketData.socketFd);
    return 0;
}
