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

#include <infiniband/verbs.h>

#include "qemu/osdep.h"

#include "virtio-rdma-ib.h"
#include "../rdma_utils.h"

int virtio_rdma_query_device(VirtIORdma *rdev, struct iovec *in,
                             struct iovec *out)
{
    struct ibv_device_attr attr = {};
    int offs;
    size_t s;

    addrconf_addr_eui48((unsigned char *)&attr.sys_image_guid,
                        (const char *)&rdev->netdev->mac);

    attr.max_mr_size = 4096;
    attr.page_size_cap = 4096;
    attr.vendor_id = 1;
    attr.vendor_part_id = 1;
    attr.hw_ver = VIRTIO_RDMA_HW_VER;
    attr.max_qp = 1024;
    attr.max_qp_wr = 1024;
    attr.device_cap_flags = 0;
    attr.max_sge = 64;
    attr.max_sge_rd = 64;
    attr.max_cq = 1024;
    attr.max_cqe = 64;
    attr.max_mr = 1024;
    attr.max_pd = 1024;
    attr.max_qp_rd_atom = 0;
    attr.max_ee_rd_atom = 0;
    attr.max_res_rd_atom = 0;
    attr.max_qp_init_rd_atom = 0;
    attr.max_ee_init_rd_atom = 0;
    attr.atomic_cap = IBV_ATOMIC_NONE;
    attr.max_ee = 0;
    attr.max_rdd = 0;
    attr.max_mw = 0;
    attr.max_raw_ipv6_qp = 0;
    attr.max_raw_ethy_qp = 0;
    attr.max_mcast_grp = 0;
    attr.max_mcast_qp_attach = 0;
    attr.max_total_mcast_qp_attach = 0;
    attr.max_ah = 1024;
    attr.max_fmr = 0;
    attr.max_map_per_fmr = 0;
    attr.max_srq = 0;
    attr.max_srq_wr = 0;
    attr.max_srq_sge = 0;
    attr.max_pkeys = 1;
    attr.local_ca_ack_delay = 0;
    attr.phys_port_cnt = VIRTIO_RDMA_PORT_CNT;

    offs = offsetof(struct ibv_device_attr, sys_image_guid);
    s = iov_from_buf(out, 1, 0, (void *)&attr + offs, sizeof(attr) - offs);

    return s == sizeof(attr) - offs ? VIRTIO_RDMA_CTRL_OK :
                                      VIRTIO_RDMA_CTRL_ERR;
}

int virtio_rdma_query_port(VirtIORdma *rdev, struct iovec *in,
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
    attr.max_mtu = attr.active_mtu = IBV_MTU_1024;
    attr.gid_tbl_len = 256;
    attr.port_cap_flags = 0;
    attr.max_msg_sz = 1024;
    attr.bad_pkey_cntr = 0;
    attr.qkey_viol_cntr = 0;
    attr.pkey_tbl_len = 1;
    attr.lid = 0;
    attr.sm_lid = 0;
    attr.lmc = 0;
    attr.max_vl_num = 1;
    attr.sm_sl = 0;
    attr.subnet_timeout = 0;
    attr.init_type_reply = 0;
    attr.active_width = 0;
    attr.active_speed = 0;
    attr.phys_state = 0;

    offs = offsetof(struct ibv_port_attr, state);
    s = iov_from_buf(out, 1, 0, (void *)&attr + offs, sizeof(attr) - offs);

    return s == sizeof(attr) - offs ? VIRTIO_RDMA_CTRL_OK :
                                      VIRTIO_RDMA_CTRL_ERR;
}
