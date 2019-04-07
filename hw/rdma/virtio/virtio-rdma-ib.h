/*
 * Virtio RDMA Device - IB verbs
 *
 * Copyright (C) 2019 Oracle
 *
 * Authors:
 *  Yuval Shaia <yuval.shaia@oracle.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "hw/virtio/virtio-rdma.h"

/* TODO: Move to uapi header file */
#define VIRTIO_RDMA_CTRL_OK    0
#define VIRTIO_RDMA_CTRL_ERR   1

enum {
    VIRTIO_CMD_QUERY_DEVICE = 10,
    VIRTIO_CMD_QUERY_PORT,
};

struct control_buf {
    uint8_t cmd;
    uint8_t status;
};

struct cmd_query_port {
    uint8_t port;
};
/* TODO: Move to uapi header file */

#define VIRTIO_RDMA_PORT_CNT    1
#define VIRTIO_RDMA_HW_VER      1

int virtio_rdma_query_device(VirtIORdma *rdev, struct iovec *in,
                             struct iovec *out);
int virtio_rdma_query_port(VirtIORdma *rdev, struct iovec *in,
                           struct iovec *out);
