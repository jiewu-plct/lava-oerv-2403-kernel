/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2021 3snic Technologies Co., Ltd */

#ifndef SSS_TOOL_COMM_H
#define SSS_TOOL_COMM_H

#define tool_err(format, ...)	pr_err(format, ##__VA_ARGS__)
#define tool_warn(format, ...)	pr_warn(format, ##__VA_ARGS__)
#define tool_info(format, ...)	pr_info(format, ##__VA_ARGS__)

#define SSS_TOOL_SHOW_ITEM_LEN 32

#define SSS_TOOL_VERSION_INFO_LEN 128

#define SSS_TOOL_EPERM        1       /* Operation not permitted */
#define SSS_TOOL_EIO          2       /* I/O error */
#define SSS_TOOL_EINVAL       3       /* Invalid argument */
#define	SSS_TOOL_EBUSY        4       /* Device or resource busy */
#define SSS_TOOL_EOPNOTSUPP   0xFF    /* Operation not supported */

enum sss_tool_driver_cmd_type {
	SSS_TOOL_GET_TX_INFO = 1,
	SSS_TOOL_GET_Q_NUM,
	SSS_TOOL_GET_TX_WQE_INFO,
	SSS_TOOL_TX_MAPPING,
	SSS_TOOL_GET_RX_INFO,
	SSS_TOOL_GET_RX_WQE_INFO,
	SSS_TOOL_GET_RX_CQE_INFO,
	SSS_TOOL_UPRINT_FUNC_EN,
	SSS_TOOL_UPRINT_FUNC_RESET,
	SSS_TOOL_UPRINT_SET_PATH,
	SSS_TOOL_UPRINT_GET_STATISTICS,
	SSS_TOOL_FUNC_TYPE,
	SSS_TOOL_GET_FUNC_IDX,
	SSS_TOOL_GET_INTER_NUM,
	SSS_TOOL_CLOSE_TX_STREAM,
	SSS_TOOL_GET_DRV_VERSION,
	SSS_TOOL_CLEAR_FUNC_STATS,
	SSS_TOOL_GET_HW_STATS,
	SSS_TOOL_CLEAR_HW_STATS,
	SSS_TOOL_GET_SELF_TEST_RES,
	SSS_TOOL_GET_CHIP_FAULT_STATS,
	SSS_TOOL_NIC_RSVD1,
	SSS_TOOL_NIC_RSVD2,
	SSS_TOOL_NIC_RSVD3,
	SSS_TOOL_GET_CHIP_ID,
	SSS_TOOL_GET_SINGLE_CARD_INFO,
	SSS_TOOL_GET_FIRMWARE_ACTIVE_STATUS,
	SSS_TOOL_ROCE_DFX_FUNC,
	SSS_TOOL_GET_DEVICE_ID,
	SSS_TOOL_GET_PF_DEV_INFO,
	SSS_TOOL_CMD_FREE_MEM,
	SSS_TOOL_GET_LOOPBACK_MODE = 32,
	SSS_TOOL_SET_LOOPBACK_MODE,
	SSS_TOOL_SET_LINK_MODE,
	SSS_TOOL_SET_PF_BW_LIMIT,
	SSS_TOOL_GET_PF_BW_LIMIT,
	SSS_TOOL_ROCE_CMD,
	SSS_TOOL_GET_POLL_WEIGHT,
	SSS_TOOL_SET_POLL_WEIGHT,
	SSS_TOOL_GET_HOMOLOGUE,
	SSS_TOOL_SET_HOMOLOGUE,
	SSS_TOOL_GET_SSET_COUNT,
	SSS_TOOL_GET_SSET_ITEMS,
	SSS_TOOL_IS_DRV_IN_VM,
	SSS_TOOL_LRO_ADPT_MGMT,
	SSS_TOOL_SET_INTER_COAL_PARAM,
	SSS_TOOL_GET_INTER_COAL_PARAM,
	SSS_TOOL_GET_CHIP_INFO,
	SSS_TOOL_GET_NIC_STATS_LEN,
	SSS_TOOL_GET_NIC_STATS_STRING,
	SSS_TOOL_GET_NIC_STATS_INFO,
	SSS_TOOL_GET_PF_ID,
	SSS_TOOL_NIC_RSVD4,
	SSS_TOOL_NIC_RSVD5,
	SSS_TOOL_DCB_QOS_INFO,
	SSS_TOOL_DCB_PFC_STATE,
	SSS_TOOL_DCB_ETS_STATE,
	SSS_TOOL_DCB_STATE,
	SSS_TOOL_QOS_DEV,
	SSS_TOOL_GET_QOS_COS,
	SSS_TOOL_GET_ULD_DEV_NAME,
	SSS_TOOL_GET_TX_TIMEOUT,
	SSS_TOOL_SET_TX_TIMEOUT,

	SSS_TOOL_RSS_CFG = 0x40,
	SSS_TOOL_RSS_INDIR,
	SSS_TOOL_PORT_ID,

	SSS_TOOL_GET_FUNC_CAP = 0x50,
	SSS_TOOL_GET_XSFP_PRESENT = 0x51,
	SSS_TOOL_GET_XSFP_INFO = 0x52,
	SSS_TOOL_DEV_NAME_TEST = 0x53,

	SSS_TOOL_GET_WIN_STAT = 0x60,
	SSS_TOOL_WIN_CSR_READ = 0x61,
	SSS_TOOL_WIN_CSR_WRITE = 0x62,
	SSS_TOOL_WIN_API_CMD_RD = 0x63,

	SSS_TOOL_VM_COMPAT_TEST = 0xFF
};

struct sss_tool_show_item {
	char name[SSS_TOOL_SHOW_ITEM_LEN];
	u8 hexadecimal; /* 0: decimal , 1: Hexadecimal */
	u8 rsvd[7];
	u64 value;
};

struct sss_tool_drv_version_info {
	char ver[SSS_TOOL_VERSION_INFO_LEN];
};

#endif /* _SSS_NIC_MT_H_ */
