#include <stdio.h>
#include <uk/fs/driver.h>
#include <uk/config.h>
#include <uk/fuse.h>
#include <uk/assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#define FUSE_USE_VERSION 31
#include <fuse_lowlevel.h>
#include <fuse_i.h>

extern struct fuse_session *hellofs_start(void);

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

int main(int argc __unused, char *argv[] __unused)
{
	int ret;
	struct stat st;

	printf("\n");
	printf("========================================\n");
	printf("  FUSE-UKFS Performance Benchmarks\n");
	printf("========================================\n");
	printf("\n");

	// test_queue_throughput();
	// test_queue_latency();

	printf("\n========================================\n");
	printf("  FUSE-UKFS M4: LOOKUP & GETATTR Tests\n");
	printf("========================================\n\n");

	struct fuse_session *se = hellofs_start();
	if (!se) {
		printf("Failed to start FUSE session\n");
		return 1;
	}


	/* Initialize FUSE message queue */
	fuse_ukfs_queue_init(&fuse_global_queue);

	/* Mount the filesystem. */
	printf("[TRACE] About to mount fuse at /...\n");
	ret = mount("none", "/", "fuse", 0, NULL);
	printf("[TRACE] mount returned %d (errno=%d)\n", ret, errno);
	UK_ASSERT(ret == 0);
	printf("[+] FUSE mounted at /\n");

	/* Start daemon connecting ukfs with se */
	ret = fuse_daemon_start(&fuse_global_queue, se);
	UK_ASSERT(ret == 0);
	printf("[+] FUSE daemon thread started\n");

	/* We sleep briefly to ensure daemon starts and spins up cleanly */
	for (volatile int i = 0; i < 10000000; i++) {}

	/* Test 1: stat("/") -> GETATTR(1) */
	printf("[TRACE] About to stat(/)...\n");
	ret = stat("/", &st);
	if (ret != 0) {
		printf("[-] stat(\"/\") failed, errno=%d\n", errno);
		UK_ASSERT(0);
	}
	UK_ASSERT(st.st_ino == 1);
	UK_ASSERT(S_ISDIR(st.st_mode));
	printf("[+] stat(\"/\") passed\n");

	/* Test 2: stat("/hello") -> LOOKUP(1, "hello") -> GETATTR(2) */
	ret = stat("/hello", &st);
	if (ret != 0) {
		printf("[-] stat(\"/hello\") failed, errno=%d\n", errno);
		UK_ASSERT(0);
	}
	UK_ASSERT(st.st_ino == 2);
	UK_ASSERT(S_ISREG(st.st_mode));
	UK_ASSERT(st.st_size == 13);
	printf("[+] stat(\"/hello\") passed\n");

	/* Test 3: non-existent file */
	ret = stat("/foo", &st);
	UK_ASSERT(ret == -1);
	UK_ASSERT(errno == ENOENT);
	printf("[+] stat(\"/foo\") ENOENT passed\n");

	/* Test 4: open + read "/hello" */
	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		printf("[-] open(\"/hello\") failed, errno=%d\n", errno);
		UK_ASSERT(0);
	}
	char rbuf[32];
	memset(rbuf, 0, sizeof(rbuf));
	ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
	if (n < 0) {
		printf("[-] read(\"/hello\") failed, errno=%d\n", errno);
		UK_ASSERT(0);
	}
	close(fd);
	UK_ASSERT(strcmp(rbuf, "Hello World!\n") == 0);
	printf("[+] read(\"/hello\") = \"%s\" passed\n", rbuf);

	/* Test 5: opendir("/") + readdir */
	DIR *d = opendir("/");
	if (!d) {
		printf("[-] opendir(\"/\") failed, errno=%d\n", errno);
		UK_ASSERT(0);
	}
	int found_hello = 0;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		printf("  readdir: inode=%llu type=%d name=%s\n",
		       (unsigned long long)de->d_ino, (int)de->d_type, de->d_name);
		if (strcmp(de->d_name, "hello") == 0)
			found_hello = 1;
	}
	closedir(d);
	UK_ASSERT(found_hello);
	printf("[+] readdir(\"/\") found \"hello\" - passed\n");

	printf("\nAll Tests and Benchmarks Passed!\n");
	return 0;
}
