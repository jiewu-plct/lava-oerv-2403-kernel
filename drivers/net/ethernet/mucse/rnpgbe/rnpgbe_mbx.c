// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2022 - 2024 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include "rnpgbe.h"
#include "rnpgbe_type.h"
#include "rnpgbe_common.h"
#include "rnpgbe_mbx.h"
#include "rnpgbe_mbx_fw.h"

/**
 *  rnpgbe_read_mbx - Reads a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox/vfnum to read
 *
 *  returns SUCCESS if it successfully read message from buffer
 **/
s32 rnpgbe_read_mbx(struct rnpgbe_hw *hw, u32 *msg, u16 size,
		    enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = RNP_ERR_MBX;

	/* limit read to size of mailbox */
	if (size > mbx->size)
		size = mbx->size;

	if (mbx->ops.read)
		ret_val = mbx->ops.read(hw, msg, size, mbx_id);

	return ret_val;
}

/**
 *  rnpgbe_write_mbx - Write a message to the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
s32 rnpgbe_write_mbx(struct rnpgbe_hw *hw, u32 *msg, u16 size,
		     enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = 0;

	if (size > mbx->size)
		ret_val = RNP_ERR_MBX;
	else if (mbx->ops.write)
		ret_val = mbx->ops.write(hw, msg, size, mbx_id);

	return ret_val;
}

static inline u16 rnpgbe_mbx_get_req(struct rnpgbe_hw *hw, int reg)
{
	/* force sync before read */
	mb();
	return ioread32(hw->hw_addr + reg) & 0xffff;
}

static inline u16 rnpgbe_mbx_get_ack(struct rnpgbe_hw *hw, int reg)
{
	/* force sync before read */
	mb();
	return (mbx_rd32(hw, reg) >> 16);
}

static inline void rnpgbe_mbx_inc_pf_req(struct rnpgbe_hw *hw,
					 enum MBX_ID mbx_id)
{
	u16 req;
	int reg;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	u32 v;

	reg = (mbx_id == MBX_CM3CPU) ? PF2CPU_COUNTER(mbx) :
					     PF2VF_COUNTER(mbx, mbx_id);
	v = mbx_rd32(hw, reg);

	req = (v & 0xffff);
	req++;
	v &= ~(0x0000ffff);
	v |= req;
	/* force sync before read */
	mb();
	mbx_wr32(hw, reg, v);

	/* update stats */
	hw->mbx.stats.msgs_tx++;
}

static inline void rnpgbe_mbx_inc_pf_ack(struct rnpgbe_hw *hw,
					 enum MBX_ID mbx_id)
{
	u16 ack;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	int reg = (mbx_id == MBX_CM3CPU) ? PF2CPU_COUNTER(mbx) :
						 PF2VF_COUNTER(mbx, mbx_id);
	u32 v = mbx_rd32(hw, reg);

	ack = (v >> 16) & 0xffff;
	ack++;
	v &= ~(0xffff0000);
	v |= (ack << 16);
	/* force sync before read */
	mb();
	mbx_wr32(hw, reg, v);

	/* update stats */
	hw->mbx.stats.msgs_rx++;
}

/**
 *  rnpgbe_check_for_msg - checks to see if someone sent us mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
s32 rnpgbe_check_for_msg(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = RNP_ERR_MBX;

	if (mbx->ops.check_for_msg)
		ret_val = mbx->ops.check_for_msg(hw, mbx_id);

	return ret_val;
}

/**
 *  rnpgbe_check_for_ack - checks to see if someone sent us ACK
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to check
 *
 *  returns SUCCESS if the Status bit was found or else ERR_MBX
 **/
s32 rnpgbe_check_for_ack(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = RNP_ERR_MBX;

	if (mbx->ops.check_for_ack)
		ret_val = mbx->ops.check_for_ack(hw, mbx_id);

	return ret_val;
}

/**
 *  rnpgbe_poll_for_msg - Wait for message notification
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification
 **/
static s32 rnpgbe_poll_for_msg(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops.check_for_msg)
		goto out;

	while (countdown && mbx->ops.check_for_msg(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		udelay(mbx->usec_delay);
	}

out:
	return countdown ? 0 : -ETIME;
}

/**
 *  rnpgbe_poll_for_ack - Wait for message acknowledgment
 *  @hw: pointer to the HW structure
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message acknowledgment
 **/
static s32 rnpgbe_poll_for_ack(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	int countdown = mbx->timeout;

	if (!countdown || !mbx->ops.check_for_ack)
		goto out;

	while (countdown && mbx->ops.check_for_ack(hw, mbx_id)) {
		countdown--;
		if (!countdown)
			break;
		udelay(mbx->usec_delay);
	}

out:
	return countdown ? 0 : RNP_ERR_MBX;
}

/**
 *  rnpgbe_read_posted_mbx - Wait for message notification and receive message
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully received a message notification and
 *  copied it into the receive buffer.
 **/
static s32 rnpgbe_read_posted_mbx(struct rnpgbe_hw *hw, u32 *msg, u16 size,
				  enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = RNP_ERR_MBX;

	if (!mbx->ops.read)
		goto out;

	ret_val = rnpgbe_poll_for_msg(hw, mbx_id);

	/* if ack received read message, otherwise we timed out */
	if (!ret_val)
		ret_val = mbx->ops.read(hw, msg, size, mbx_id);
out:
	return ret_val;
}

/**
 *  rnpgbe_write_posted_mbx - Write a message to the mailbox, wait for ack
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: id of mailbox to write
 *
 *  returns SUCCESS if it successfully copied message into the buffer and
 *  received an ack to that message within delay * timeout period
 **/
static s32 rnpgbe_write_posted_mbx(struct rnpgbe_hw *hw, u32 *msg, u16 size,
				   enum MBX_ID mbx_id)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	s32 ret_val = RNP_ERR_MBX;

	/* exit if either we can't write or there isn't a defined timeout */
	if (!mbx->ops.write || !mbx->timeout)
		goto out;

	/* send msg and hold buffer lock */
	ret_val = mbx->ops.write(hw, msg, size, mbx_id);

	/* if msg sent wait until we receive an ack */
	if (!ret_val)
		ret_val = rnpgbe_poll_for_ack(hw, mbx_id);

out:
	return ret_val;
}

/**
 *  rnpgbe_check_for_msg_pf - checks to see if the VF has sent mail
 *  @hw: pointer to the HW structure
 *  @mbx_id: the mbxidx
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
static s32 rnpgbe_check_for_msg_pf(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	s32 ret_val = RNP_ERR_MBX;
	u16 hw_req_count = 0;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;

	if (mbx_id == MBX_CM3CPU) {
		hw_req_count = rnpgbe_mbx_get_req(hw, CPU2PF_COUNTER(mbx));
		if (hw_req_count != 0 && hw_req_count != hw->mbx.cpu_req) {
			ret_val = 0;
			hw->mbx.stats.reqs++;
		}

	} else {
		if (rnpgbe_mbx_get_req(hw, VF2PF_COUNTER(mbx, mbx_id)) !=
		    hw->mbx.vf_req[mbx_id]) {
			ret_val = 0;
			hw->mbx.stats.reqs++;
		}
	}

	return ret_val;
}

/**
 *  rnpgbe_check_for_ack_pf - checks to see if the VF has ACKed
 *  @hw: pointer to the HW structure
 *  @mbx_id: the mbx idx
 *
 *  returns SUCCESS if the VF has set the Status bit or else ERR_MBX
 **/
static s32 rnpgbe_check_for_ack_pf(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	s32 ret_val = RNP_ERR_MBX;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;

	if (mbx_id == MBX_CM3CPU) {
		if (rnpgbe_mbx_get_ack(hw, CPU2PF_COUNTER(mbx)) !=
		    hw->mbx.cpu_ack) {
			ret_val = 0;
			hw->mbx.stats.acks++;
		}
	} else {
		if (rnpgbe_mbx_get_ack(hw, VF2PF_COUNTER(mbx, mbx_id)) !=
		    hw->mbx.vf_ack[mbx_id]) {
			ret_val = 0;
			hw->mbx.stats.acks++;
		}
	}

	return ret_val;
}

/**
 *  rnpgbe_obtain_mbx_lock_pf - obtain mailbox lock
 *  @hw: pointer to the HW structure
 *  @mbx_id: the VF index or CPU
 *
 *  return SUCCESS if we obtained the mailbox lock
 **/
static s32 rnpgbe_obtain_mbx_lock_pf(struct rnpgbe_hw *hw, enum MBX_ID mbx_id)
{
	int try_cnt = 5000;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	u32 CTRL_REG = (mbx_id == MBX_CM3CPU) ? PF2CPU_MBOX_CTRL(mbx) :
						      PF2VF_MBOX_CTRL(mbx, mbx_id);

	while (try_cnt-- > 0) {
		/* Take ownership of the buffer */
		mbx_wr32(hw, CTRL_REG, MBOX_CTRL_PF_HOLD_SHM);
		/* force sync before read */
		wmb();
		/* reserve mailbox for cm3 use */
		if (mbx_rd32(hw, CTRL_REG) & MBOX_CTRL_PF_HOLD_SHM)
			return 0;
		usleep_range(100, 200);
	}

	rnpgbe_err("%s: failed to get:%d lock\n", __func__, mbx_id);

	return -EPERM;
}

/**
 *  rnpgbe_write_mbx_pf - Places a message in the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: the VF index
 *
 *  returns SUCCESS if it successfully copied message into the buffer
 **/
static s32 rnpgbe_write_mbx_pf(struct rnpgbe_hw *hw, u32 *msg, u16 size,
			       enum MBX_ID mbx_id)
{
	s32 ret_val = 0;
	u16 i;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	u32 DATA_REG = (mbx_id == MBX_CM3CPU) ? CPU_PF_SHM_DATA(mbx) :
						      PF_VF_SHM_DATA(mbx, mbx_id);
	u32 CTRL_REG = (mbx_id == MBX_CM3CPU) ? PF2CPU_MBOX_CTRL(mbx) :
						      PF2VF_MBOX_CTRL(mbx, mbx_id);

	if (size > RNP_VFMAILBOX_SIZE)
		return -EINVAL;

	/* lock the mailbox to prevent pf/vf/cpu race condition */
	ret_val = rnpgbe_obtain_mbx_lock_pf(hw, mbx_id);
	if (ret_val)
		goto out_no_write;

	/* copy the caller specified message to the mailbox memory buffer */
	for (i = 0; i < size; i++) {
		mbx_wr32(hw, DATA_REG + i * 4, msg[i]);
		rnpgbe_logd(LOG_MBX_OUT, "  w-mbx:0x%x <= 0x%x\n",
			    DATA_REG + i * 4, msg[i]);
	}

	/* flush msg and acks as we are overwriting the message buffer */
	if (mbx_id == MBX_CM3CPU) {
		hw->mbx.cpu_ack = rnpgbe_mbx_get_ack(hw, CPU2PF_COUNTER(mbx));
	} else {
		hw->mbx.vf_ack[mbx_id] =
			rnpgbe_mbx_get_ack(hw, VF2PF_COUNTER(mbx, mbx_id));
	}
	rnpgbe_mbx_inc_pf_req(hw, mbx_id);

	/* Interrupt VF/CM3 to tell it a message
	 * has been sent and release buffer
	 */
	mbx_wr32(hw, CTRL_REG, MBOX_CTRL_REQ);

out_no_write:

	return ret_val;
}

/**
 *  rnpgbe_read_mbx_pf - Read a message from the mailbox
 *  @hw: pointer to the HW structure
 *  @msg: The message buffer
 *  @size: Length of buffer
 *  @mbx_id: the mbx idx
 *
 *  This function copies a message from the mailbox buffer to the caller's
 *  memory buffer.  The presumption is that the caller knows that there was
 *  a message due to a VF/CPU request so no polling for message is needed.
 **/
static s32 rnpgbe_read_mbx_pf(struct rnpgbe_hw *hw, u32 *msg, u16 size,
			      enum MBX_ID mbx_id)
{
	s32 ret_val = -EIO;
	u32 i;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;
	u32 BUF_REG = (mbx_id == MBX_CM3CPU) ? CPU_PF_SHM_DATA(mbx) :
						     PF_VF_SHM_DATA(mbx, mbx_id);
	u32 CTRL_REG = (mbx_id == MBX_CM3CPU) ? PF2CPU_MBOX_CTRL(mbx) :
						      PF2VF_MBOX_CTRL(mbx, mbx_id);
	if (size > RNP_VFMAILBOX_SIZE)
		return -EINVAL;
	/* lock the mailbox to prevent pf/vf race condition */
	ret_val = rnpgbe_obtain_mbx_lock_pf(hw, mbx_id);
	if (ret_val)
		goto out_no_read;

	/* force sync before read */
	mb();
	/* copy the message from the mailbox memory buffer */
	for (i = 0; i < size; i++) {
		msg[i] = mbx_rd32(hw, BUF_REG + 4 * i);
		rnpgbe_logd(LOG_MBX_IN, "  r-mbx:0x%x => 0x%x\n",
			    BUF_REG + 4 * i, msg[i]);
	}
	mbx_wr32(hw, BUF_REG, 0);

	/* update req. used by rnpvf_check_for_msg_vf  */
	if (mbx_id == MBX_CM3CPU) {
		hw->mbx.cpu_req = rnpgbe_mbx_get_req(hw, CPU2PF_COUNTER(mbx));
	} else {
		hw->mbx.vf_req[mbx_id] =
			rnpgbe_mbx_get_req(hw, VF2PF_COUNTER(mbx, mbx_id));
	}
	/* this ack maybe too earier? */
	/* Acknowledge receipt and release mailbox, then we're done */
	rnpgbe_mbx_inc_pf_ack(hw, mbx_id);

	/* free ownership of the buffer */
	mbx_wr32(hw, CTRL_REG, 0);

out_no_read:

	return ret_val;
}

static void rnpgbe_mbx_reset(struct rnpgbe_hw *hw)
{
	int idx, v;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;

	for (idx = 0; idx < hw->max_vfs; idx++) {
		v = mbx_rd32(hw, VF2PF_COUNTER(mbx, idx));
		hw->mbx.vf_req[idx] = v & 0xffff;
		hw->mbx.vf_ack[idx] = (v >> 16) & 0xffff;
		mbx_wr32(hw, PF2VF_MBOX_CTRL(mbx, idx), 0);
	}
	/* reset pf->cm3 status */
	v = mbx_rd32(hw, CPU2PF_COUNTER(mbx));
	hw->mbx.cpu_req = v & 0xffff;
	hw->mbx.cpu_ack = (v >> 16) & 0xffff;

	mbx_wr32(hw, PF2CPU_MBOX_CTRL(mbx), 0);

	if (PF_VF_MBOX_MASK_LO(mbx))
		wr32(hw, PF_VF_MBOX_MASK_LO(mbx), 0);
	if (PF_VF_MBOX_MASK_HI(mbx))
		wr32(hw, PF_VF_MBOX_MASK_HI(mbx), 0);

	wr32(hw, CPU_PF_MBOX_MASK(mbx), 0);
}

static int rnpgbe_mbx_configure_pf(struct rnpgbe_hw *hw, int nr_vec,
				   bool enable)
{
	int idx = 0;
	u32 v;
	struct rnpgbe_mbx_info *mbx = &hw->mbx;

	if (enable) {
		for (idx = 0; idx < hw->max_vfs; idx++) {
			v = mbx_rd32(hw, VF2PF_COUNTER(mbx, idx));
			hw->mbx.vf_req[idx] = v & 0xffff;
			hw->mbx.vf_ack[idx] = (v >> 16) & 0xffff;

			mbx_wr32(hw, PF2VF_MBOX_CTRL(mbx, idx), 0);
		}
		/* reset pf->cm3 status */
		v = mbx_rd32(hw, CPU2PF_COUNTER(mbx));
		hw->mbx.cpu_req = v & 0xffff;
		hw->mbx.cpu_ack = (v >> 16) & 0xffff;
		/* release   pf->cm3 buffer lock */
		mbx_wr32(hw, PF2CPU_MBOX_CTRL(mbx), 0);

		/* allow VF to PF MBX IRQ */
		for (idx = 0; idx < hw->max_vfs; idx++)
			mbx_wr32(hw, VF2PF_MBOX_VEC(mbx, idx), nr_vec);

		if (PF_VF_MBOX_MASK_LO(mbx))
			wr32(hw, PF_VF_MBOX_MASK_LO(mbx), 0);

		if (PF_VF_MBOX_MASK_HI(mbx))
			wr32(hw, PF_VF_MBOX_MASK_HI(mbx), 0);

		/* bind cm3cpu mbx to irq */
		wr32(hw, CPU2PF_MBOX_VEC(mbx), nr_vec);
		wr32(hw, CPU_PF_MBOX_MASK(mbx), 0);

	} else {
		if (PF_VF_MBOX_MASK_LO(mbx))
			wr32(hw, PF_VF_MBOX_MASK_LO(mbx), 0xffffffff);
		if (PF_VF_MBOX_MASK_HI(mbx))
			wr32(hw, PF_VF_MBOX_MASK_HI(mbx), 0xffffffff);

		/* disable CM3CPU to PF MBX IRQ */
		wr32(hw, CPU_PF_MBOX_MASK(mbx), 0xffffffff);

		/* reset vf->pf status/ctrl */
		for (idx = 0; idx < hw->max_vfs; idx++)
			mbx_wr32(hw, PF2VF_MBOX_CTRL(mbx, idx), 0);
		mbx_wr32(hw, PF2CPU_MBOX_CTRL(mbx), 0);
		wr32(hw, RNP_DMA_DUMY, 0);
	}

	return 0;
}

unsigned int rnpgbe_mbx_change_timeout(struct rnpgbe_hw *hw, int timeout_ms)
{
	unsigned int old_timeout = hw->mbx.timeout;

	hw->mbx.timeout = timeout_ms * 1000 / hw->mbx.usec_delay;

	return old_timeout;
}

/**
 *  rnpgbe_init_mbx_params_pf - set initial values for pf mailbox
 *  @hw: pointer to the HW structure
 *
 *  Initializes the hw->mbx struct to correct values for pf mailbox
 */
s32 rnpgbe_init_mbx_params_pf(struct rnpgbe_hw *hw)
{
	struct rnpgbe_mbx_info *mbx = &hw->mbx;

	mbx->usec_delay = 100;
	mbx->timeout = (4 * 1000 * 1000) / mbx->usec_delay;

	mbx->stats.msgs_tx = 0;
	mbx->stats.msgs_rx = 0;
	mbx->stats.reqs = 0;
	mbx->stats.acks = 0;
	mbx->stats.rsts = 0;

	mbx->size = RNP_VFMAILBOX_SIZE;

	mutex_init(&mbx->lock);
	rnpgbe_mbx_reset(hw);
	return 0;
}

struct rnpgbe_mbx_operations mbx_ops_generic = {
	.init_params = rnpgbe_init_mbx_params_pf,
	.read = rnpgbe_read_mbx_pf,
	.write = rnpgbe_write_mbx_pf,
	.read_posted = rnpgbe_read_posted_mbx,
	.write_posted = rnpgbe_write_posted_mbx,
	.check_for_msg = rnpgbe_check_for_msg_pf,
	.check_for_ack = rnpgbe_check_for_ack_pf,
	.configure = rnpgbe_mbx_configure_pf,
};
