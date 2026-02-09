/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/fs/driver.h>
#include <uk/fs.h>
#include <uk/errptr.h>
#include <uk/print.h>

#include <uk/fuse.h>

/*
 * Skeleton vopen - entry point for mounting
 */
static
const struct uk_file *fusefs_vopen(union uk_fs_vopen_vol vol __unused,
				   unsigned long flags __unused,
				   union uk_fs_vopen_data data __unused,
				   size_t fmt __unused)
{
	uk_pr_info("ukfusefs: vopen called! Driver is registered correctly.\n");
	return ERR2PTR(-ENOSYS); /* Not implemented yet */
}

/* Register the "fuse" filesystem driver */
UK_FS_DRIVER_REGISTER(fuse, fusefs_vopen, UK_FS_VOPEN_VOL_IGNORE);
