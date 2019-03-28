/*
 * PCI Virtio Network Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_NET_PCI_H
#define QEMU_VIRTIO_NET_PCI_H

#include "hw/virtio/virtio-net.h"
#include "virtio-pci.h"

typedef struct VirtIONetPCI VirtIONetPCI;

/*
 * virtio-net-pci: This extends VirtioPCIProxy.
 */
#define TYPE_VIRTIO_NET_PCI_GENERIC "virtio-net-pci"
#define TYPE_VIRTIO_NET_PCI "virtio-net-pci-base"
#define VIRTIO_NET_PCI(obj) \
        OBJECT_CHECK(VirtIONetPCI, (obj), TYPE_VIRTIO_NET_PCI)

struct VirtIONetPCI {
    VirtIOPCIProxy parent_obj;
    VirtIONet vdev;
};

#endif
