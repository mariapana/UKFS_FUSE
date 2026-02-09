#include <stdio.h>
#include <uk/fs/driver.h>
#include <uk/config.h>

int main(int argc, char *argv[])
{
    printf("Fuse Test App\n");
    
    /* Check if driver is registered */
    const struct uk_fs_drv *drv = uk_fs_driver("fuse");
    
    if (drv) {
        printf("SUCCESS: Fuse driver found!\n");
        printf("  fstype: %s\n", drv->fstype);
    } else {
        printf("FAILURE: Fuse driver NOT found!\n");
    }
    return 0;
}
