#include "qemu/osdep.h"

#define RDMA_CM_DEFAULT_UNIX_SOCKET_PATH "/tmp/rdma_cm0"
/** default listen backlog (number of sockets not accepted) */
#define RDMA_CM_SERVER_LISTEN_BACKLOG 10


/* arguments given by the user */
typedef struct RdmaCmServerArgs {
    const char *unix_socket_path;
} RdmaCMServerArgs;

typedef struct RdmaCmServer {
    int sock_fd;
} RdmaCMServer;

static void rdma_cm_server_usage(const char *progname)
{
    printf("Usage: %s [OPTION]...\n"
           "  -h: show this help\n"
           "  -S <unix-socket-path>: path to the unix socket to listen to\n"
           "     default " RDMA_CM_DEFAULT_UNIX_SOCKET_PATH "\n",
           progname);
}

static void rdma_cm_server_help(const char *progname)
{
    fprintf(stderr, "Try '%s -h' for more information.\n", progname);
}

static void rdma_cm_server_parse_args(RdmaCMServerArgs *args, int argc, char *argv[])
{
    int c;

    while ((c = getopt(argc, argv, "hS:")) != -1) {
        switch (c) {
        case 'h': /* help */
            rdma_cm_server_usage(argv[0]);
            exit(0);
            break;

        case 'S': /* unix socket path */
            args->unix_socket_path = optarg;
            break;

        default:
            rdma_cm_server_help(argv[0]);
            exit(1);
            break;
        }
    }
}

static int rdma_cm_server_start(RdmaCMServer *server, RdmaCMServerArgs *args)
{
    struct sockaddr_un sun;
    int sock_fd, ret = 0;

    /* create the unix listening socket */
    sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        fprintf(stderr, "cannot create socket: %s\n", strerror(errno));
        return -1;
    }

    sun.sun_family = AF_UNIX;
    ret = snprintf(sun.sun_path, sizeof(sun.sun_path), "%s",
                   args->unix_socket_path);
    if (ret < 0 || ret >= sizeof(sun.sun_path)) {
        fprintf(stderr, "could not copy unix socket path\n");
        goto err_close_sock;
    }
    if (bind(sock_fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        fprintf(stderr, "cannot connect to %s: %s\n", sun.sun_path,
                strerror(errno));
        goto err_close_sock;
    }

    if (listen(sock_fd, RDMA_CM_SERVER_LISTEN_BACKLOG) < 0) {
        fprintf(stderr, "listen() failed: %s\n", strerror(errno));
        goto err_close_sock;
    }

    server->sock_fd = sock_fd;
    return 0;

err_close_sock:
    close(sock_fd);
    return ret;
}

int main(int argc, char *argv[])
{
    RdmaCMServer server;
    RdmaCMServerArgs args = {
        .unix_socket_path = RDMA_CM_DEFAULT_UNIX_SOCKET_PATH,
    };

    /* parse arguments, will exit on error */
    rdma_cm_server_parse_args(&args, argc, argv);

    if (!rdma_cm_server_start(&server, &args)) {
        fprintf(stderr, "cannot bind\n");
        exit(1);
    }

    return 0;
}
 
