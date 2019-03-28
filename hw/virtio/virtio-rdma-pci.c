/*
 * Virtio rdma PCI Bindings
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

#include "hw/virtio/virtio-net-pci.h"
#include "hw/virtio/virtio-rdma.h"
#include "virtio-pci.h"
#include "qapi/error.h"

typedef struct VirtIORdmaPCI VirtIORdmaPCI;

/*
 * virtio-rdma-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_RDMA_PCI "virtio-rdma-pci-base"
#define VIRTIO_RDMA_PCI(obj) \
        OBJECT_CHECK(VirtIORdmaPCI, (obj), TYPE_VIRTIO_RDMA_PCI)

struct VirtIORdmaPCI {
    VirtIOPCIProxy parent_obj;
    VirtIORdma vdev;
};

static Property virtio_rdma_properties[] = {
    DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                    VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
    DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 3),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_rdma_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIORdmaPCI *dev = VIRTIO_RDMA_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);
    VirtIONetPCI *vnet_pci;
    PCIDevice *func0;

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus));
    object_property_set_bool(OBJECT(vdev), true, "realized", errp);

    func0 = pci_get_function_0(&vpci_dev->pci_dev);
    /* Break if not virtio device in slot 0 */
    if (strcmp(object_get_typename(OBJECT(func0)), TYPE_VIRTIO_NET_PCI_GENERIC)) {
        error_setg(errp, "Device on %x.0 is type %s but must be %s",
                   PCI_SLOT(vpci_dev->pci_dev.devfn),
		   object_get_typename(OBJECT(func0)),
		   TYPE_VIRTIO_NET_PCI_GENERIC);
        return;
    }
    vnet_pci = VIRTIO_NET_PCI(func0);
    dev->vdev.netdev = &vnet_pci->vdev;
}

static void virtio_rdma_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    VirtioPCIClass *vpciklass = VIRTIO_PCI_CLASS(klass);

    k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    k->device_id = PCI_DEVICE_ID_VIRTIO_RDMA;
    k->revision = VIRTIO_PCI_ABI_VERSION;
    k->class_id = PCI_CLASS_NETWORK_OTHER;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    dc->props = virtio_rdma_properties;
    vpciklass->realize = virtio_rdma_pci_realize;
}

static void virtio_rdma_pci_instance_init(Object *obj)
{
    VirtIORdmaPCI *dev = VIRTIO_RDMA_PCI(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_RDMA);
    /*
    object_property_add_alias(obj, "bootindex", OBJECT(&dev->vdev),
                              "bootindex", &error_abort);
    */
}

static const VirtioPCIDeviceTypeInfo virtio_rdma_pci_info = {
    .base_name             = TYPE_VIRTIO_RDMA_PCI,
    .generic_name          = "virtio-rdma-pci",
    .transitional_name     = "virtio-rdma-pci-transitional",
    .non_transitional_name = "virtio-rdma-pci-non-transitional",
    .instance_size = sizeof(VirtIORdmaPCI),
    .instance_init = virtio_rdma_pci_instance_init,
    .class_init    = virtio_rdma_pci_class_init,
};

static void virtio_rdma_pci_register(void)
{
    virtio_pci_types_register(&virtio_rdma_pci_info);
}

type_init(virtio_rdma_pci_register)
