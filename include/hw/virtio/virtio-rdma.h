/*
 * Virtio RDMA Device
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

#ifndef QEMU_VIRTIO_RDMA_H
#define QEMU_VIRTIO_RDMA_H

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-net.h"

#define TYPE_VIRTIO_RDMA "virtio-rdma-device"
#define VIRTIO_RDMA(obj) \
        OBJECT_CHECK(VirtIORdma, (obj), TYPE_VIRTIO_RDMA)

typedef struct VirtIORdma {
    VirtIODevice parent_obj;
    VirtQueue *ctrl_vq;
    VirtIONet *netdev;
} VirtIORdma;

#endif
