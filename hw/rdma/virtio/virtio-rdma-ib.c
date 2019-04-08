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
#include "../rdma_rm.h"
#include "../rdma_backend.h"

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

int virtio_rdma_create_cq(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out)
{
    struct cmd_create_cq cmd = {};
    struct rsp_create_cq rsp = {};
    size_t s;
    int rc;

    s = iov_to_buf(in, 1, 0, &cmd, sizeof(cmd));
    if (s != sizeof(cmd)) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    /* TODO: Define MAX_CQE */
#define MAX_CQE 1024
    /* TODO: Check MAX_CQ */
    if (cmd.cqe > MAX_CQE) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    printf("%s: %d\n", __func__, cmd.cqe);

    /* TODO: Create VirtQ */

    rc = rdma_rm_alloc_cq(rdev->rdma_dev_res, rdev->backend_dev, cmd.cqe,
                          &rsp.cqn, NULL);
    if (rc) {
        /* TODO: Destroy VirtQ */
        return VIRTIO_RDMA_CTRL_ERR;
    }

    printf("%s: %d\n", __func__, rsp.cqn);

    s = iov_from_buf(out, 1, 0, &rsp, sizeof(rsp));

    return s == sizeof(rsp) ? VIRTIO_RDMA_CTRL_OK :
                              VIRTIO_RDMA_CTRL_ERR;
}

int virtio_rdma_destroy_cq(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out)
{
    struct cmd_destroy_cq cmd = {};
    size_t s;

    s = iov_to_buf(in, 1, 0, &cmd, sizeof(cmd));
    if (s != sizeof(cmd)) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    printf("%s: %d\n", __func__, cmd.cqn);

    /* TODO: Destroy VirtQ */

    rdma_rm_dealloc_cq(rdev->rdma_dev_res, cmd.cqn);

    return VIRTIO_RDMA_CTRL_OK;
}

int virtio_rdma_create_pd(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out)
{
    struct rsp_create_pd rsp = {};
    size_t s;
    int rc;

    /* TODO: Check MAX_PD */

    /* TODO: ctx */
    rc = rdma_rm_alloc_pd(rdev->rdma_dev_res, rdev->backend_dev, &rsp.pdn,
                          0);
    if (rc)
        return VIRTIO_RDMA_CTRL_ERR;

    printf("%s: %d\n", __func__, rsp.pdn);

    s = iov_from_buf(out, 1, 0, &rsp, sizeof(rsp));

    return s == sizeof(rsp) ? VIRTIO_RDMA_CTRL_OK :
                              VIRTIO_RDMA_CTRL_ERR;
}

int virtio_rdma_destroy_pd(VirtIORdma *rdev, struct iovec *in,
                          struct iovec *out)
{
    struct cmd_destroy_pd cmd = {};
    size_t s;

    s = iov_to_buf(in, 1, 0, &cmd, sizeof(cmd));
    if (s != sizeof(cmd)) {
        return VIRTIO_RDMA_CTRL_ERR;
    }

    printf("%s: %d\n", __func__, cmd.pdn);

    rdma_rm_dealloc_cq(rdev->rdma_dev_res, cmd.pdn);

    return VIRTIO_RDMA_CTRL_OK;
}

static void virtio_rdma_init_dev_caps(VirtIORdma *rdev)
{
    rdev->dev_attr.max_qp_wr = 1024;
}

int virtio_rdma_init_ib(VirtIORdma *rdev)
{
    int rc;

    virtio_rdma_init_dev_caps(rdev);

    rdev->rdma_dev_res = g_malloc0(sizeof(RdmaDeviceResources));
    rdev->backend_dev = g_malloc0(sizeof(RdmaBackendDev));

    rc = rdma_backend_init(rdev->backend_dev, NULL, rdev->rdma_dev_res,
                           rdev->backend_device_name,
                           rdev->backend_port_num, &rdev->dev_attr,
                           &rdev->mad_chr);
    if (rc) {
        rdma_error_report("Fail to initialize backend device");
        return rc;
    }

    rc = rdma_rm_init(rdev->rdma_dev_res, &rdev->dev_attr);
    if (rc) {
        rdma_error_report("Fail to initialize resource manager");
        return rc;
    }

    /* rdma_backend_start(rdev->backend_dev); */

    return 0;
}

void virtio_rdma_fini_ib(VirtIORdma *rdev)
{
    /* rdma_backend_stop(rdev->backend_dev); */
    rdma_rm_fini(rdev->rdma_dev_res, rdev->backend_dev,
                 rdev->backend_eth_device_name);
    rdma_backend_fini(rdev->backend_dev);
    g_free(rdev->rdma_dev_res);
    g_free(rdev->backend_dev);
}
