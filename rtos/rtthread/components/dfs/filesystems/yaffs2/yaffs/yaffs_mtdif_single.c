/*
 * YAFFS: Yet Another Flash File System. A NAND-flash specific file system.
 *
 * Copyright (C) 2002-2018 Aleph One Ltd.
 *
 * Created by Charles Manning <charles@aleph1.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "yportenv.h"

#include "yaffs_mtdif.h"

#include "linux/mtd/mtd.h"
#include "linux/types.h"
#include "linux/time.h"
#include "linux/mtd/nand.h"
#include "linux/kernel.h"
#include "linux/version.h"
#include "linux/types.h"

#include "yaffs_trace.h"
#include "yaffs_guts.h"
#include "yaffs_linux.h"


int nandmtd_erase_block(struct yaffs_dev *dev, int block_no)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);
    u32 addr =
        ((loff_t) block_no) * dev->param.total_bytes_per_chunk *
        dev->param.chunks_per_block;
    struct erase_info ei;
    int retval = 0;

    ei.mtd = mtd;
    ei.addr = addr;
    ei.len = dev->param.total_bytes_per_chunk * dev->param.chunks_per_block;
    ei.time = 1000;
    ei.retries = 2;
    ei.callback = NULL;
    ei.priv = (u_long) dev;

    retval = mtd->erase(mtd, &ei);

    if (retval == 0)
        return YAFFS_OK;

    return YAFFS_FAIL;
}


static  int yaffs_mtd_write(struct yaffs_dev *dev, int nand_chunk,
                            const u8 *data, int data_len,
                            const u8 *oob, int oob_len)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);
    loff_t addr;
    struct mtd_oob_ops ops;
    int retval;

    addr = ((loff_t) nand_chunk) * dev->param.total_bytes_per_chunk;
    memset(&ops, 0, sizeof(ops));
    ops.mode = MTD_OPS_AUTO_OOB;
    ops.len = (data) ? data_len : 0;
    ops.ooblen = oob_len;
    ops.datbuf = (u8 *)data;
    ops.oobbuf = (u8 *)oob;

    retval = mtd->write_oob(mtd, addr, &ops);
    if (retval)
    {
        yaffs_trace(YAFFS_TRACE_MTD,
                    "write_oob failed, chunk %d, mtd error %d",
                    nand_chunk, retval);
    }
    return retval ? YAFFS_FAIL : YAFFS_OK;
}

static int yaffs_mtd_read(struct yaffs_dev *dev, int nand_chunk,
                          u8 *data, int data_len,
                          u8 *oob, int oob_len,
                          enum yaffs_ecc_result *ecc_result)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);
    loff_t addr;
    struct mtd_oob_ops ops;
    int retval;

    addr = ((loff_t) nand_chunk) * dev->data_bytes_per_chunk;
    memset(&ops, 0, sizeof(ops));
    ops.mode = MTD_OPS_AUTO_OOB;
    ops.len = (data) ? data_len : 0;
    ops.ooblen = oob_len;
    ops.datbuf = data;
    ops.oobbuf = oob;

    /* Read page and oob using MTD.
     * Check status and determine ECC result.
     */
    retval = mtd->read_oob(mtd, addr, &ops);
    if (retval)
        yaffs_trace(YAFFS_TRACE_MTD,
                    "read_oob failed, chunk %d, mtd error %d",
                    nand_chunk, retval);

    switch (retval)
    {
    case 0:
        /* no error */
        if (ecc_result)
            *ecc_result = YAFFS_ECC_RESULT_NO_ERROR;
        break;

    case -EUCLEAN:
        /* MTD's ECC fixed the data */
        if (ecc_result)
            *ecc_result = YAFFS_ECC_RESULT_FIXED;
        dev->n_ecc_fixed++;
        break;

    case -EBADMSG:
    default:
        /* MTD's ECC could not fix the data */
        dev->n_ecc_unfixed++;
        if (ecc_result)
            *ecc_result = YAFFS_ECC_RESULT_UNFIXED;
        return YAFFS_FAIL;
    }

    return YAFFS_OK;
}

static  int yaffs_mtd_erase(struct yaffs_dev *dev, int block_no)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);

    loff_t addr;
    struct erase_info ei;
    int retval = 0;
    u32 block_size;

    block_size = dev->param.total_bytes_per_chunk *
                 dev->param.chunks_per_block;
    addr = ((loff_t) block_no) * block_size;

    ei.mtd = mtd;
    ei.addr = addr;
    ei.len = block_size;
    ei.time = 1000;
    ei.retries = 2;
    ei.callback = NULL;
    ei.priv = (u_long) dev;

    retval = mtd->erase(mtd, &ei);

    if (retval == 0)
        return YAFFS_OK;

    return YAFFS_FAIL;
}

static int yaffs_mtd_mark_bad(struct yaffs_dev *dev, int block_no)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);
    int blocksize = dev->param.chunks_per_block * dev->data_bytes_per_chunk;
    int retval;

    yaffs_trace(YAFFS_TRACE_BAD_BLOCKS, "marking block %d bad", block_no);

    retval = mtd->block_markbad(mtd, (loff_t) blocksize * block_no);
    return (retval) ? YAFFS_FAIL : YAFFS_OK;
}

static int yaffs_mtd_check_bad(struct yaffs_dev *dev, int block_no)
{
    struct mtd_info *mtd = yaffs_dev_to_mtd(dev);
    int blocksize = dev->param.chunks_per_block * dev->data_bytes_per_chunk;
    int retval;

    yaffs_trace(YAFFS_TRACE_BAD_BLOCKS, "checking block %d bad", block_no);

    retval = mtd->block_isbad(mtd, (loff_t) blocksize * block_no);
    return (retval) ? YAFFS_FAIL : YAFFS_OK;
}

static int yaffs_mtd_initialise(struct yaffs_dev *dev)
{
    return YAFFS_OK;
}

static int yaffs_mtd_deinitialise(struct yaffs_dev *dev)
{
    return YAFFS_OK;
}



void yaffs_mtd_drv_install(struct yaffs_dev *dev)
{
    struct yaffs_driver *drv = &dev->drv;

    drv->drv_write_chunk_fn = yaffs_mtd_write;
    drv->drv_read_chunk_fn = yaffs_mtd_read;
    drv->drv_erase_fn = yaffs_mtd_erase;
    drv->drv_mark_bad_fn = yaffs_mtd_mark_bad;
    drv->drv_check_bad_fn = yaffs_mtd_check_bad;
    drv->drv_initialise_fn = yaffs_mtd_initialise;
    drv->drv_deinitialise_fn = yaffs_mtd_deinitialise;
}
