/*
 * QEMU paravirtual RDMA - rdmacm-mux implementation
 *
 * Copyright (C) 2018 Oracle
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *     Yuval Shaia <yuval.shaia@oracle.com>
 *     Marcel Apfelbaum <marcel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "sys/poll.h"
#include "sys/ioctl.h"
#include "pthread.h"
#include "syslog.h"

#include "linux/if_addr.h"
#include "libmnl/libmnl.h"
#include "linux/rtnetlink.h"
#include "net/if.h"

#include "infiniband/verbs.h"
#include "infiniband/umad.h"
#include "infiniband/umad_types.h"
#include "infiniband/umad_sa.h"
#include "infiniband/umad_cm.h"

#include "rdmacm-mux.h"

#define SCALE_US 1000
#define COMMID_TTL 2 /* How many SCALE_US a context of MAD session is saved */
#define SLEEP_SECS 5 /* This is used both in poll() and thread */
#define SERVER_LISTEN_BACKLOG 10
#define MAX_CLIENTS 4096
#define MAD_RMPP_VERSION 0
#define MAD_METHOD_MASK0 0x8

#define IB_USER_MAD_LONGS_PER_METHOD_MASK (128 / (8 * sizeof (long )))

#define CM_REQ_DGID_POS      80
#define CM_SIDR_REQ_DGID_POS 44

/* The below can be override by command line parameter */
#define UNIX_SOCKET_PATH "/var/run/rdmacm-mux"
#define RDMA_DEVICE "rxe0"
#define RDMA_PORT_NUM 1

typedef struct RdmaCmServerArgs {
    char unix_socket_path[PATH_MAX];
    char rdma_dev_name[NAME_MAX];
    int rdma_port_num;
} RdmaCMServerArgs;

typedef struct CommId2FdEntry {
    int fd;
    int ttl; /* Initialized to 2, decrement each timeout, entry delete when 0 */
    __be64 gid_ifid;
} CommId2FdEntry;

typedef struct RdmaCmUMadAgent {
    int port_id;
    int agent_id;
    GHashTable *gid2fd; /* Used to find fd of a given gid */
    GHashTable *commid2fd; /* Used to find fd on of a given comm_id */
} RdmaCmUMadAgent;

typedef struct RdmaCmServer {
    bool run;
    RdmaCMServerArgs args;
    struct pollfd fds[MAX_CLIENTS];
    int nfds;
    RdmaCmUMadAgent umad_agent;
    pthread_t umad_recv_thread;
    pthread_rwlock_t lock;
} RdmaCMServer;

RdmaCMServer server = {0};

void signal_handler(int signo);

static void usage(const char *progname)
{
    printf("Usage: %s [OPTION]...\n"
           "Start a RDMA-CM multiplexer\n"
           "\n"
           "\t-h                    Show this help\n"
           "\t-s unix-socket-path   Path to unix socket to listen on (default %s)\n"
           "\t-d rdma-device-name   Name of RDMA device to register with (default %s)\n"
           "\t-p rdma-device-port   Port number of RDMA device to register with (default %d)\n",
           progname, UNIX_SOCKET_PATH, RDMA_DEVICE, RDMA_PORT_NUM);
}

static void help(const char *progname)
{
    fprintf(stderr, "Try '%s -h' for more information.\n", progname);
}

static void parse_args(int argc, char *argv[])
{
    int c;
    char unix_socket_path[PATH_MAX];

    strcpy(unix_socket_path, UNIX_SOCKET_PATH);
    strncpy(server.args.rdma_dev_name, RDMA_DEVICE, NAME_MAX - 1);
    server.args.rdma_port_num = RDMA_PORT_NUM;

    while ((c = getopt(argc, argv, "hs:d:p:")) != -1) {
        switch (c) {
        case 'h':
            usage(argv[0]);
            exit(0);

        case 's':
            /* This is temporary, final name will build below */
            strncpy(unix_socket_path, optarg, PATH_MAX);
            break;

        case 'd':
            strncpy(server.args.rdma_dev_name, optarg, NAME_MAX - 1);
            break;

        case 'p':
            server.args.rdma_port_num = atoi(optarg);
            break;

        default:
            help(argv[0]);
            exit(1);
        }
    }

    /* Build unique unix-socket file name */
    snprintf(server.args.unix_socket_path, PATH_MAX, "%s-%s-%d",
             unix_socket_path, server.args.rdma_dev_name,
             server.args.rdma_port_num);

    syslog(LOG_INFO, "unix_socket_path=%s", server.args.unix_socket_path);
    syslog(LOG_INFO, "rdma-device-name=%s", server.args.rdma_dev_name);
    syslog(LOG_INFO, "rdma-device-port=%d", server.args.rdma_port_num);
}

static void hash_tbl_alloc(void)
{

    server.umad_agent.gid2fd = g_hash_table_new_full(g_int64_hash,
                                                     g_int64_equal,
                                                     g_free, g_free);
    server.umad_agent.commid2fd = g_hash_table_new_full(g_int_hash,
                                                        g_int_equal,
                                                        g_free, g_free);
}

static void hash_tbl_free(void)
{
    if (server.umad_agent.commid2fd) {
        g_hash_table_destroy(server.umad_agent.commid2fd);
    }
    if (server.umad_agent.gid2fd) {
        g_hash_table_destroy(server.umad_agent.gid2fd);
    }
}

static int hash_tbl_search_fd_by_ifid(int *fd, __be64 *gid_ifid)
{
    int *fd1;

    pthread_rwlock_rdlock(&server.lock);
    fd1 = g_hash_table_lookup(server.umad_agent.gid2fd, gid_ifid);
    if (!fd1) {
        /* Let's try IPv4 */
        *gid_ifid |= 0x00000000ffff0000;
        fd1 = g_hash_table_lookup(server.umad_agent.gid2fd, gid_ifid);
    }
    pthread_rwlock_unlock(&server.lock);

    if (!fd1) {
        syslog(LOG_WARNING, "Can't find matching for ifid 0x%llx\n", *gid_ifid);
        return ENOENT;
    }

    *fd = *fd1;

    return 0;
}

static int hash_tbl_search_fd_by_comm_id(uint32_t comm_id, int *fd,
                                         __be64 *gid_idid)
{
    CommId2FdEntry *fde;

    pthread_rwlock_rdlock(&server.lock);
    fde = g_hash_table_lookup(server.umad_agent.commid2fd, &comm_id);
    pthread_rwlock_unlock(&server.lock);

    if (!fde) {
        syslog(LOG_WARNING, "Can't find matching for comm_id 0x%x\n", comm_id);
        return ENOENT;
    }

    *fd = fde->fd;
    *gid_idid = fde->gid_ifid;

    return 0;
}

static void add_fd_ifid_pair(int fd, uint64_t gid_ifid)
{

    pthread_rwlock_wrlock(&server.lock);
    g_hash_table_insert(server.umad_agent.gid2fd, g_memdup(&gid_ifid,
                        sizeof(gid_ifid)), g_memdup(&fd, sizeof(fd)));
    pthread_rwlock_unlock(&server.lock);

    syslog(LOG_INFO, "0x%lx registered on socket %d", gid_ifid, fd);
}

static void delete_fd_ifid_pair(int fd, uint64_t gid_ifid)
{

    pthread_rwlock_wrlock(&server.lock);
    g_hash_table_remove(server.umad_agent.gid2fd, g_memdup(&gid_ifid,
                         sizeof(gid_ifid)));
    pthread_rwlock_unlock(&server.lock);

    syslog(LOG_INFO, "0x%lx unregistered on socket %d", gid_ifid, fd);
}

static void hash_tbl_save_fd_comm_id_pair(int fd, uint32_t comm_id,
                                          uint64_t gid_ifid)
{
    CommId2FdEntry fde = {fd, COMMID_TTL, gid_ifid};

    pthread_rwlock_wrlock(&server.lock);
    g_hash_table_insert(server.umad_agent.commid2fd,
                        g_memdup(&comm_id, sizeof(comm_id)),
                        g_memdup(&fde, sizeof(fde)));
    pthread_rwlock_unlock(&server.lock);
}

static gboolean remove_old_comm_ids(gpointer key, gpointer value,
                                    gpointer user_data)
{
    CommId2FdEntry *fde = (CommId2FdEntry *)value;

    return !fde->ttl--;
}

static gboolean remove_entry_from_gid2fd(gpointer key, gpointer value,
                                         gpointer user_data)
{
     return *(int *)value == *(int *)user_data;
}

static void hash_tbl_remove_fd_ifid_pair(int fd)
{
    pthread_rwlock_wrlock(&server.lock);
    g_hash_table_foreach_remove(server.umad_agent.gid2fd,
                                remove_entry_from_gid2fd, (gpointer)&fd);
    pthread_rwlock_unlock(&server.lock);
}

static int get_fd(const char *mad, int *fd, __be64 *gid_ifid)
{
    struct umad_hdr *hdr = (struct umad_hdr *)mad;
    char *data = (char *)hdr + sizeof(*hdr);
    int32_t comm_id;
    uint16_t attr_id = be16toh(hdr->attr_id);
    int rc;

    switch (attr_id) {
    case UMAD_CM_ATTR_REQ:
        memcpy(gid_ifid, data + CM_REQ_DGID_POS, sizeof(*gid_ifid));
        rc = hash_tbl_search_fd_by_ifid(fd, gid_ifid);
        break;

    case UMAD_CM_ATTR_SIDR_REQ:
        memcpy(gid_ifid, data + CM_SIDR_REQ_DGID_POS, sizeof(*gid_ifid));
        rc = hash_tbl_search_fd_by_ifid(fd, gid_ifid);
        break;

    case UMAD_CM_ATTR_REP:
        /* Fall through */
    case UMAD_CM_ATTR_REJ:
        /* Fall through */
    case UMAD_CM_ATTR_DREQ:
        /* Fall through */
    case UMAD_CM_ATTR_DREP:
        /* Fall through */
    case UMAD_CM_ATTR_RTU:
        data += sizeof(comm_id);
        /* Fall through */
    case UMAD_CM_ATTR_SIDR_REP:
        memcpy(&comm_id, data, sizeof(comm_id));
        rc = hash_tbl_search_fd_by_comm_id(comm_id, fd, gid_ifid);
        break;

    default:
        rc = EINVAL;
        syslog(LOG_WARNING, "Unsupported attr_id 0x%x\n", attr_id);
    }

    return rc;
}

static void *umad_recv_thread_func(void *args)
{
    int rc;
    RdmaCmMuxMsg msg = {0};
    int fd = -2;

    while (server.run) {
        do {
            msg.umad_len = sizeof(msg.umad.mad);
            rc = umad_recv(server.umad_agent.port_id, &msg.umad, &msg.umad_len,
                           SLEEP_SECS * SCALE_US);
            if ((rc == -EIO) || (rc == -EINVAL)) {
                syslog(LOG_CRIT, "Fatal error while trying to read MAD");
            }

            if (rc == -ETIMEDOUT) {
                g_hash_table_foreach_remove(server.umad_agent.commid2fd,
                                            remove_old_comm_ids, NULL);
            }
        } while (rc && server.run);

        if (server.run) {
            rc = get_fd(msg.umad.mad, &fd, &msg.hdr.sgid.global.interface_id);
            if (rc) {
                continue;
            }

            send(fd, &msg, sizeof(msg), 0);
        }
    }

    return NULL;
}

static int netlink_route_update(char *ifname, union ibv_gid *gid, __u16 type)
{
    char buf[MNL_SOCKET_BUFFER_SIZE];
    struct nlmsghdr *nlh;
    struct ifaddrmsg *ifm;
    struct mnl_socket *nl;
    int ret;
    uint32_t ipv4;

    nl = mnl_socket_open(NETLINK_ROUTE);
    if (!nl) {
        syslog(LOG_WARNING, "Fail to connect to netlink\n");
        return -EIO;
    }

    ret = mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID);
    if (ret < 0) {
        syslog(LOG_WARNING, "Fail to bind to netlink\n");
        goto out;
    }

    nlh = mnl_nlmsg_put_header(buf);
    nlh->nlmsg_type = type;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
    nlh->nlmsg_seq = 1;

    ifm = mnl_nlmsg_put_extra_header(nlh, sizeof(*ifm));
    ifm->ifa_index = if_nametoindex(ifname);
    if (gid->global.subnet_prefix) {
        ifm->ifa_family = AF_INET6;
        ifm->ifa_prefixlen = 64;
        ifm->ifa_flags = IFA_F_PERMANENT;
        ifm->ifa_scope = RT_SCOPE_UNIVERSE;
        mnl_attr_put(nlh, IFA_ADDRESS, sizeof(*gid), gid);
    } else {
        ifm->ifa_family = AF_INET;
        ifm->ifa_prefixlen = 24;
        memcpy(&ipv4, (char *)&gid->global.interface_id + 4, sizeof(ipv4));
        mnl_attr_put(nlh, IFA_LOCAL, 4, &ipv4);
    }

    ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
    if (ret < 0) {
        syslog(LOG_WARNING, "Fail to send msg to to netlink\n");
        goto out;
    }

    ret = 0;

out:
    mnl_socket_close(nl);
    return ret;
}

static int read_and_process(int fd)
{
    int rc;
    RdmaCmMuxMsg msg = {0};
    struct umad_hdr *hdr;
    uint32_t *comm_id;
    uint16_t attr_id;

    rc = recv(fd, &msg, sizeof(msg), 0);

    if (rc < 0 && errno != EWOULDBLOCK) {
        return EIO;
    }

    if (rc == 0) {
        return EPIPE;
    }

    switch (msg.hdr.msg_type) {
    case RDMACM_MUX_MSG_TYPE_REG:
        add_fd_ifid_pair(fd, msg.hdr.sgid.global.interface_id);
        rc = netlink_route_update(msg.hdr.ifname, &msg.hdr.sgid, RTM_NEWADDR);
        break;

    case RDMACM_MUX_MSG_TYPE_UNREG:
        delete_fd_ifid_pair(fd, msg.hdr.sgid.global.interface_id);
        rc = netlink_route_update(msg.hdr.ifname, &msg.hdr.sgid, RTM_DELADDR);
        break;

    case RDMACM_MUX_MSG_TYPE_MAD:
        /* If this is REQ or REP then store the pair comm_id,fd to be later
         * used for other messages where gid is unknown */
        hdr = (struct umad_hdr *)msg.umad.mad;
        attr_id = be16toh(hdr->attr_id);
        if ((attr_id == UMAD_CM_ATTR_REQ) || (attr_id == UMAD_CM_ATTR_DREQ) ||
            (attr_id == UMAD_CM_ATTR_SIDR_REQ) ||
            (attr_id == UMAD_CM_ATTR_REP) || (attr_id == UMAD_CM_ATTR_DREP)) {
            comm_id = (uint32_t *)(msg.umad.mad + sizeof(*hdr));
            hash_tbl_save_fd_comm_id_pair(fd, *comm_id,
                                          msg.hdr.sgid.global.interface_id);
        }

        rc = umad_send(server.umad_agent.port_id, server.umad_agent.agent_id,
                       &msg.umad, msg.umad_len, 1, 0);
        if (rc) {
            syslog(LOG_WARNING, "Fail to send MAD message, err=%d", rc);
        }
        rc = 0;
        break;

    default:
        syslog(LOG_WARNING, "Got invalid message (%d) from %d",
               msg.hdr.msg_type, fd);
        rc = EINVAL;
    }

    return rc;
}

static int accept_all(void)
{
    int fd, rc = 0;;

    pthread_rwlock_wrlock(&server.lock);

    do {
        if ((server.nfds + 1) > MAX_CLIENTS) {
            syslog(LOG_WARNING, "Too many clients (%d)", server.nfds);
            rc = EIO;
            goto out;
        }

        fd = accept(server.fds[0].fd, NULL, NULL);
        if (fd < 0) {
            if (errno != EWOULDBLOCK) {
                syslog(LOG_WARNING, "accept() failed");
                rc = EIO;
                goto out;
            }
            break;
        }

        server.fds[server.nfds].fd = fd;
        server.fds[server.nfds].events = POLLIN;
        server.nfds++;
    } while (fd != -1);

out:
    pthread_rwlock_unlock(&server.lock);
    return rc;
}

static void compress_fds(void)
{
    int i,j;
    int closed = 0;

    pthread_rwlock_wrlock(&server.lock);

    for (i = 1; i < server.nfds; i++) {
        if (!server.fds[i].fd) {
            closed++;
            for (j = i; j < server.nfds; j++) {
                server.fds[j].fd = server.fds[j + 1].fd;
            }
        }
    }

    server.nfds -= closed;

    pthread_rwlock_unlock(&server.lock);
}

static void close_fd(int idx)
{
    close(server.fds[idx].fd);
    syslog(LOG_INFO, "Socket %d closed\n", server.fds[idx].fd);
    hash_tbl_remove_fd_ifid_pair(server.fds[idx].fd);
    server.fds[idx].fd = 0;
}

static void run(void)
{
    int rc, nfds, i;
    bool compress = false;

    syslog(LOG_INFO, "Service started");

    while (server.run) {
        rc = poll(server.fds, server.nfds, SLEEP_SECS * SCALE_US);
        if (rc < 0) {
            if (errno != EINTR) {
                syslog(LOG_WARNING, "poll() failed");
            }
            continue;
        }

        if (rc == 0) {
            continue;
        }

        nfds = server.nfds;
        for (i = 0; i < nfds; i++) {
            if (server.fds[i].revents == 0) {
                continue;
            }

            if (server.fds[i].revents != POLLIN) {
                if (i == 0) {
                    syslog(LOG_NOTICE, "Unexpected poll() event (0x%x)\n",
                           server.fds[i].revents);
                } else {
                    close_fd(i);
                    compress = true;
                }
                continue;
            }

            if (i == 0) {
                rc = accept_all();
                if (rc) {
                    continue;
                }
            } else {
                rc = read_and_process(server.fds[i].fd);
                if (rc) {
                    close_fd(i);
                    compress = true;
                }
            }
        }

        if (compress) {
            compress = false;
            compress_fds();
        }
    }
}

static void fini_listener(void)
{
    int i;

    if (server.fds[0].fd <= 0) {
        return;
    }

    for (i = server.nfds - 1; i >= 0; i--) {
        if (server.fds[i].fd) {
            close(server.fds[i].fd);
        }
    }

    unlink(server.args.unix_socket_path);
}

static void fini_umad(void)
{
    if (server.umad_agent.agent_id) {
        umad_unregister(server.umad_agent.port_id, server.umad_agent.agent_id);
    }

    if (server.umad_agent.port_id) {
        umad_close_port(server.umad_agent.port_id);
    }

    hash_tbl_free();
}

static void fini(void)
{
    if (server.umad_recv_thread) {
        pthread_join(server.umad_recv_thread, NULL);
        server.umad_recv_thread = 0;
    }
    fini_umad();
    fini_listener();
    pthread_rwlock_destroy(&server.lock);

    syslog(LOG_INFO, "Service going down");
}

static int init_listener(void)
{
    struct sockaddr_un sun;
    int rc, on = 1;

    server.fds[0].fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server.fds[0].fd < 0) {
        syslog(LOG_ALERT, "socket() failed");
        return EIO;
    }

    rc = setsockopt(server.fds[0].fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on,
                    sizeof(on));
    if (rc < 0) {
        syslog(LOG_ALERT, "setsockopt() failed");
        rc = EIO;
        goto err;
    }

    rc = ioctl(server.fds[0].fd, FIONBIO, (char *)&on);
    if (rc < 0) {
        syslog(LOG_ALERT, "ioctl() failed");
        rc = EIO;
        goto err;
    }

    if (strlen(server.args.unix_socket_path) >= sizeof(sun.sun_path)) {
        syslog(LOG_ALERT,
               "Invalid unix_socket_path, size must be less than %ld\n",
               sizeof(sun.sun_path));
        rc = EINVAL;
        goto err;
    }

    sun.sun_family = AF_UNIX;
    rc = snprintf(sun.sun_path, sizeof(sun.sun_path), "%s",
                  server.args.unix_socket_path);
    if (rc < 0 || rc >= sizeof(sun.sun_path)) {
        syslog(LOG_ALERT, "Could not copy unix socket path\n");
        rc = EINVAL;
        goto err;
    }

    rc = bind(server.fds[0].fd, (struct sockaddr *)&sun, sizeof(sun));
    if (rc < 0) {
        syslog(LOG_ALERT, "bind() failed");
        rc = EIO;
        goto err;
    }

    rc = listen(server.fds[0].fd, SERVER_LISTEN_BACKLOG);
    if (rc < 0) {
        syslog(LOG_ALERT, "listen() failed");
        rc = EIO;
        goto err;
    }

    server.fds[0].events = POLLIN;
    server.nfds = 1;
    server.run = true;

    return 0;

err:
    close(server.fds[0].fd);
    return rc;
}

static int init_umad(void)
{
    long method_mask[IB_USER_MAD_LONGS_PER_METHOD_MASK];

    server.umad_agent.port_id = umad_open_port(server.args.rdma_dev_name,
                                               server.args.rdma_port_num);

    if (server.umad_agent.port_id < 0) {
        syslog(LOG_WARNING, "umad_open_port() failed");
        return EIO;
    }

    memset(&method_mask, 0, sizeof(method_mask));
    method_mask[0] = MAD_METHOD_MASK0;
    server.umad_agent.agent_id = umad_register(server.umad_agent.port_id,
                                               UMAD_CLASS_CM,
                                               UMAD_SA_CLASS_VERSION,
                                               MAD_RMPP_VERSION, method_mask);
    if (server.umad_agent.agent_id < 0) {
        syslog(LOG_WARNING, "umad_register() failed");
        return EIO;
    }

    hash_tbl_alloc();

    return 0;
}

void signal_handler(int signo)
{
    static bool warned = false;

    /* Prevent stop if clients are connected */
    if (server.nfds != 1) {
        if (!warned) {
            syslog(LOG_WARNING,
                    "Can't stop while active client exist, resend SIGINT to overid");
            warned = true;
            return;
        }
    }

    if (signo == SIGINT) {
        server.run = false;
        fini();
    }

    exit(0);
}

static int init(void)
{
    int rc;

    rc = init_listener();
    if (rc) {
        return rc;
    }

    rc = init_umad();
    if (rc) {
        return rc;
    }

    pthread_rwlock_init(&server.lock, 0);

    rc = pthread_create(&server.umad_recv_thread, NULL, umad_recv_thread_func,
                        NULL);
    if (!rc) {
        return rc;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int rc;

    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        syslog(LOG_ERR, "Fail to install SIGINT handler\n");
        return EAGAIN;
    }

    memset(&server, 0, sizeof(server));

    parse_args(argc, argv);

    rc = init();
    if (rc) {
        syslog(LOG_ERR, "Fail to initialize server (%d)\n", rc);
        rc = EAGAIN;
        goto out;
    }

    run();

out:
    fini();

    return rc;
}
