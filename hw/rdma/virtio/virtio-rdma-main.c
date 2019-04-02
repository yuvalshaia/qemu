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

#include <infiniband/verbs.h>

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "hw/virtio/virtio.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-rdma.h"
#include "include/standard-headers/linux/virtio_ids.h"

#include "../rdma_utils.h"

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

/* TODO: Move to virtio-rdma-ib.h */
#define VIRTIO_RDMA_PORT_CNT    1
#define VIRTIO_RDMA_HW_VER	1
/* TODO: Move to virtio-rdma-ib.h */

/* TODO: Move to virtio-rdma-ib.c */
static int virtio_rdma_query_device(VirtIORdma *rdev, struct iovec *in,
                                    struct iovec *out)
{
    struct ibv_device_attr attr = {};
    int offs;
    size_t s;

    addrconf_addr_eui48((unsigned char *)&attr.sys_image_guid,
                        (const char *)&rdev->netdev->mac);
    attr.hw_ver = VIRTIO_RDMA_HW_VER;
    attr.phys_port_cnt = VIRTIO_RDMA_PORT_CNT;

    offs = offsetof(struct ibv_device_attr, sys_image_guid);
    s = iov_from_buf(out, 1, 0, (void *)&attr + offs, sizeof(attr) - offs);

    return s == sizeof(attr) - offs ? VIRTIO_RDMA_CTRL_OK :
                                      VIRTIO_RDMA_CTRL_ERR;
}

static int virtio_rdma_query_port(VirtIORdma *rdev, struct iovec *in,
                                  struct iovec *out)
{
    struct ibv_port_attr attr = {};
    struct cmd_query_port cmd = {};
    int offs;
    size_t s;

    s = iov_to_buf(in, 1, 0, &cmd, sizeof(cmd));
    if (s != sizeof(cmd)) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    if (cmd.port != 1) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    attr.state = IBV_PORT_ACTIVE;
    attr.gid_tbl_len = 256;

    offs = offsetof(struct ibv_port_attr, state);
    s = iov_from_buf(out, 1, 0, (void *)&attr + offs, sizeof(attr) - offs);

    return s == sizeof(attr) - offs ? VIRTIO_RDMA_CTRL_OK :
                                      VIRTIO_RDMA_CTRL_ERR;
}
/* TODO: Move to virtio-rdma-ib.c */

static void virtio_rdma_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIORdma *r = VIRTIO_RDMA(vdev);
    struct control_buf cb;
    VirtQueueElement *e;
    size_t s;

    virtio_queue_set_notification(vq, 0);

    for (;;) {
        e = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!e) {
            break;
        }

        if (iov_size(e->in_sg, e->in_num) < sizeof(cb.status) ||
            iov_size(e->out_sg, e->out_num) < sizeof(cb.cmd)) {
            virtio_error(vdev, "Got invalid message size");
            virtqueue_detach_element(vq, e, 0);
            g_free(e);
            break;
        }

        s = iov_to_buf(&e->out_sg[0], 1, 0, &cb.cmd, sizeof(cb.cmd));
        if (s != sizeof(cb.cmd)) {
            cb.status = VIRTIO_RDMA_CTRL_ERR;
        } else {
	printf("cmd=%d\n", cb.cmd);
            switch (cb.cmd) {
            case VIRTIO_CMD_QUERY_DEVICE:
                cb.status = virtio_rdma_query_device(r, &e->out_sg[1],
                                                     &e->in_sg[0]);
                break;
            case VIRTIO_CMD_QUERY_PORT:
                cb.status = virtio_rdma_query_port(r, &e->out_sg[1],
                                                   &e->in_sg[0]);
                break;
            default:
                cb.status = VIRTIO_RDMA_CTRL_ERR;
            }
        }
        cb.status = VIRTIO_RDMA_CTRL_OK;
        printf("status=%d\n", cb.status);
        s = iov_from_buf(&e->in_sg[1], 1, 0, &cb.status, sizeof(cb.status));
        assert(s == sizeof(cb.status));

        virtqueue_push(vq, e, sizeof(cb.status));
        virtio_notify(vdev, vq);
    }

    virtio_queue_set_notification(vq, 1);
}

static void virtio_rdma_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIORdma *r = VIRTIO_RDMA(dev);

    virtio_init(vdev, "virtio-rdma", VIRTIO_ID_RDMA, 1024);

    r->ctrl_vq = virtio_add_queue(vdev, 64, virtio_rdma_handle_ctrl);
}

static void virtio_rdma_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    virtio_del_queue(vdev, 0);

    virtio_cleanup(vdev);
}

static uint64_t virtio_rdma_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    /* virtio_add_feature(&features, VIRTIO_NET_F_MAC); */

    vdev->backend_features = features;

    return features;
}

static void virtio_rdma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = virtio_rdma_device_realize;
    vdc->unrealize = virtio_rdma_device_unrealize;
    vdc->get_features = virtio_rdma_get_features;
}

static const TypeInfo virtio_rdma_info = {
    .name = TYPE_VIRTIO_RDMA,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIORdma),
    .class_init = virtio_rdma_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_rdma_info);
}

type_init(virtio_register_types)
