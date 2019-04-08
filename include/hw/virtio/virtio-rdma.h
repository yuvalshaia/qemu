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

#include <infiniband/verbs.h>

#include "chardev/char-fe.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-net.h"

#define TYPE_VIRTIO_RDMA "virtio-rdma-device"
#define VIRTIO_RDMA(obj) \
        OBJECT_CHECK(VirtIORdma, (obj), TYPE_VIRTIO_RDMA)

typedef struct RdmaBackendDev RdmaBackendDev;
typedef struct RdmaDeviceResources RdmaDeviceResources;
struct ibv_device_attr;

typedef struct VirtIORdma {
    VirtIODevice parent_obj;
    VirtQueue *ctrl_vq;
    VirtIONet *netdev;
    RdmaBackendDev *backend_dev;
    RdmaDeviceResources *rdma_dev_res;
    CharBackend mad_chr;
    char *backend_eth_device_name;
    char *backend_device_name;
    uint8_t backend_port_num;
    struct ibv_device_attr dev_attr;
} VirtIORdma;

#endif
