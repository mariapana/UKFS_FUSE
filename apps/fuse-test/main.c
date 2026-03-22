#include <stdio.h>
#include <uk/fs/driver.h>
#include <uk/config.h>
#include <uk/fuse.h>
#include <uk/assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>
#include <fuse_i.h>

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

/* Test 4: libfuse Reply Wiring (from M3) */
void test_fuse_reply_wiring(void)
{
	struct fuse_ukfs_request ukfs_req;
	struct fuse_req *fake_req;
	struct fuse_session fake_se;
	struct fuse_entry_param fake_entry;
	struct stat fake_stat;
	char fake_buf[] = "hello fuse";
	int ret;
	
	printf("Test 4: libfuse Reply Wiring\n");
	printf("----------------------------\n");
	
	/* Initialize the UKFS request and fake FUSE session */
	memset(&fake_se, 0, sizeof(fake_se));
	fake_se.conn.no_interrupt = 1;
	
	printf("  Testing fuse_reply_err...\n");
	memset(&ukfs_req, 0, sizeof(ukfs_req));
	uk_semaphore_init(&ukfs_req.done, 0);
	fake_req = calloc(1, sizeof(*fake_req));
	fake_req->ukfs_req = &ukfs_req;
	fake_req->se = &fake_se;
	fake_req->ref_cnt = 1;
	
	ret = fuse_reply_err(fake_req, ENOENT);
	UK_ASSERT(ret == 0);
	UK_ASSERT(ukfs_req.error == -ENOENT);
	uk_semaphore_down(&ukfs_req.done); /* Should not block */
	
	printf("  Testing fuse_reply_entry...\n");
	memset(&ukfs_req, 0, sizeof(ukfs_req));
	uk_semaphore_init(&ukfs_req.done, 0);
	fake_req = calloc(1, sizeof(*fake_req));
	fake_req->ukfs_req = &ukfs_req;
	fake_req->se = &fake_se;
	fake_req->ref_cnt = 1;
	
	memset(&fake_entry, 0, sizeof(fake_entry));
	fake_entry.ino = 42;
	fake_entry.attr.st_size = 1024;
	
	ret = fuse_reply_entry(fake_req, &fake_entry);
	UK_ASSERT(ret == 0);
	UK_ASSERT(ukfs_req.error == 0);
	UK_ASSERT(ukfs_req.reply_len == sizeof(struct fuse_entry_param));
	UK_ASSERT(((struct fuse_entry_param *)ukfs_req.reply_data)->ino == 42);
	uk_semaphore_down(&ukfs_req.done);
	free(ukfs_req.reply_data);
	
	printf("  Testing fuse_reply_attr...\n");
	memset(&ukfs_req, 0, sizeof(ukfs_req));
	uk_semaphore_init(&ukfs_req.done, 0);
	fake_req = calloc(1, sizeof(*fake_req));
	fake_req->ukfs_req = &ukfs_req;
	fake_req->se = &fake_se;
	fake_req->ref_cnt = 1;
	
	memset(&fake_stat, 0, sizeof(fake_stat));
	fake_stat.st_ino = 99;
	
	ret = fuse_reply_attr(fake_req, &fake_stat, 1.0);
	UK_ASSERT(ret == 0);
	UK_ASSERT(ukfs_req.error == 0);
	UK_ASSERT(ukfs_req.reply_len == sizeof(struct stat));
	UK_ASSERT(((struct stat *)ukfs_req.reply_data)->st_ino == 99);
	uk_semaphore_down(&ukfs_req.done);
	free(ukfs_req.reply_data);
	
	printf("  Testing fuse_reply_buf...\n");
	memset(&ukfs_req, 0, sizeof(ukfs_req));
	uk_semaphore_init(&ukfs_req.done, 0);
	fake_req = calloc(1, sizeof(*fake_req));
	fake_req->ukfs_req = &ukfs_req;
	fake_req->se = &fake_se;
	fake_req->ref_cnt = 1;
	
	ret = fuse_reply_buf(fake_req, fake_buf, sizeof(fake_buf));
	UK_ASSERT(ret == 0);
	UK_ASSERT(ukfs_req.error == 0);
	UK_ASSERT(ukfs_req.reply_len == sizeof(fake_buf));
	UK_ASSERT(strcmp((char *)ukfs_req.reply_data, "hello fuse") == 0);
	uk_semaphore_down(&ukfs_req.done);
	free(ukfs_req.reply_data);
	
	printf("libfuse integration verified!\n\n");
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
	test_fuse_reply_wiring();
	
	printf("========================================\n");
	printf("  All tests passed!\n");
	printf("========================================\n");
	printf("\n");
	
	return 0;
}
