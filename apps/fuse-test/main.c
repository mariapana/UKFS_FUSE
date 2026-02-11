#include <stdio.h>
#include <uk/fs/driver.h>
#include <uk/config.h>
#include <uk/fuse.h>
#include <uk/assert.h>
#include <string.h>

/* Test 1: Queue operations */
void test_queue_operations(void)
{
	struct fuse_ukfs_queue q;
	struct fuse_ukfs_request req1, req2;
	struct fuse_ukfs_request *r;
	
	printf("Test 1: Queue Operations\n");
	printf("-------------------------\n");
	
	/* Initialize queue */
	fuse_ukfs_queue_init(&q);
	printf("Queue initialized\n");
	
	/* Initialize requests */
	memset(&req1, 0, sizeof(req1));
	req1.opcode = FUSE_UKFS_LOOKUP;
	req1.unique = 1;
	uk_semaphore_init(&req1.done, 0);
	
	memset(&req2, 0, sizeof(req2));
	req2.opcode = FUSE_UKFS_GETATTR;
	req2.unique = 2;
	uk_semaphore_init(&req2.done, 0);
	
	/* Push requests */
	fuse_ukfs_queue_push(&q, &req1);
	fuse_ukfs_queue_push(&q, &req2);
	printf("Pushed 2 requests\n");
	
	/* Pop requests (FIFO order) */
	r = fuse_ukfs_queue_pop(&q);
	UK_ASSERT(r->unique == 1);
	printf("Popped request 1 (FIFO verified)\n");
	
	r = fuse_ukfs_queue_pop(&q);
	UK_ASSERT(r->unique == 2);
	printf("Popped request 2\n");
	
	printf("\n");
}

/* Test 2: Daemon thread */
void test_daemon_thread(void)
{
	struct fuse_ukfs_queue q;
	struct fuse_ukfs_request req;
	int ret;
	
	printf("Test 2: Daemon Thread\n");
	printf("---------------------\n");
	
	/* Initialize queue and start daemon */
	fuse_ukfs_queue_init(&q);
	ret = fuse_daemon_start(&q);
	UK_ASSERT(ret == 0);
	printf("Daemon thread started\n");
	
	/* Send a test request */
	memset(&req, 0, sizeof(req));
	req.opcode = FUSE_UKFS_LOOKUP;
	req.unique = 42;
	uk_semaphore_init(&req.done, 0);
	
	printf("  Sending request to daemon...\n");
	fuse_ukfs_queue_push(&q, &req);
	
	/* Wait for daemon to process it */
	uk_semaphore_down(&req.done);
	
	printf("Daemon processed request (error=%d)\n", req.error);
	UK_ASSERT(req.error == -ENOSYS);  /* Expected for skeleton */
	
	printf("\n");
}

/* Test 3: Driver registration (from M1) */
void test_driver_registration(void)
{
	const struct uk_fs_drv *drv;
	
	printf("Test 3: Driver Registration\n");
	printf("---------------------------\n");
	
	/* Check if driver is registered */
	drv = uk_fs_driver("fuse");
	
	if (drv) {
		printf("FUSE driver found\n");
		printf("  fstype: %s\n", drv->fstype);
	} else {
		printf("FUSE driver NOT found\n");
	}
	
	printf("\n");
}

int main(int argc, char *argv[])
{
	printf("\n");
	printf("========================================\n");
	printf("  FUSE-UKFS Milestone 2 Tests\n");
	printf("========================================\n");
	printf("\n");
	
	test_queue_operations();
	test_daemon_thread();
	test_driver_registration();
	
	printf("========================================\n");
	printf("  All tests passed!\n");
	printf("========================================\n");
	printf("\n");
	
	return 0;
}
