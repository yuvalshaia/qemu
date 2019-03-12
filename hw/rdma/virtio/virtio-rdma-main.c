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

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "hw/virtio/virtio.h"
#include "qemu/error-report.h"
#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-rdma.h"
#include "include/standard-headers/linux/virtio_ids.h"

static void virtio_rdma_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

    virtio_init(vdev, "virtio-rdma", VIRTIO_ID_RDMA, 1024);
}

static void virtio_rdma_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);

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
