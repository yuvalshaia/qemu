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
    VIRTIO_CMD_CREATE_CQ,
    VIRTIO_CMD_DESTROY_CQ,
};

struct control_buf {
    uint8_t cmd;
    uint8_t status;
};

struct cmd_query_port {
    uint8_t port;
};

struct cmd_create_cq {
    uint32_t cqe;
};

struct rsp_create_cq {
    uint32_t cqn;
};

struct cmd_destroy_cq {
    uint32_t cqn;
};

/* TODO: Move to uapi header file */

#define VIRTIO_RDMA_PORT_CNT    1
#define VIRTIO_RDMA_HW_VER      1

int virtio_rdma_init_ib(VirtIORdma *rdev);
void virtio_rdma_fini_ib(VirtIORdma *rdev);

int virtio_rdma_query_device(VirtIORdma *rdev, struct iovec *in,
                             struct iovec *out);
int virtio_rdma_query_port(VirtIORdma *rdev, struct iovec *in,
                           struct iovec *out);
int virtio_rdma_create_cq(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out);
int virtio_rdma_destroy_cq(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out);
