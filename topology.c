#include "topology.h"
#include <stdlib.h>
#include <stdbool.h>

#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>

typedef struct topologydata {
    struct sockaddr_in *servers[TOPOLOGY_MAX_SIZE];
    int size;
} TopologyData;

static void topology_cleanup(const Topology *tp) {
    TopologyData *tpd = (TopologyData *)sl->self;

    // free all of the strings allocated from the heap
    long i;
    for (i = 0L; i< (sld->size); i++) {
        char *str_clear;
        sl->get(sl, i, &str_clear);
        free(str_clear);
    }

    // free our data struct
    free(sld->array);
    free(sld);

    // free ourselves from the shackles of the ADT library
    free((void *) tpd);
}

static bool topology_add_address(const Topology *tp, int socket, char *hostname, char *port) {
    // get our tp data
    TopologyData *tpd = tp->self;

    // make a new address
    struct sockaddr_in *serverAddr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
    memset(serverAddr, 0, sizeof(struct sockaddr_in));
    serverAddr->sin_family = AF_INET;
    inet_aton(hostname, &(serverAddr->sin_addr));
    serverAddr->sin_port = htons(atoi(port));

    // bind the address to the socket
    int result = bind(openSocket, (struct sockaddr*)serverAddr, sizeof(struct sockaddr_in));

    if (result < 0) {
        // big bad failure, everything must go
        fprintf(stderr, "Topology socket bind failed.\n");
        return false;
    }

    // add the address to the struct
    int current_size = tpd->size;
    (tpd->servers)[current_size] = serverAddr;
    tpd->size += 1;

    // everything seems good
    return true;
}

static Topology template = {NULL, topology_cleanup, topology_add_address};

const Topology *Topology_create() {
    Topology *tp = (Topology *)malloc(sizeof(Topology));
    TopologyData *tpd = (TopologyData *)malloc(sizeof(TopologyData));
    *tp = template;
    tp->self = tpd;
    return tp;
}
