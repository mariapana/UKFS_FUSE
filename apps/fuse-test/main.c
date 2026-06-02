#include <stdio.h>
#include <errno.h>
#include <uk/assert.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#include <fuse_lowlevel.h>

/* hello_ll.c is compiled with -Dmain=fuse_backend_main */
extern int fuse_backend_main(int argc, char *argv[]);

static void backend_thread_fn(void *arg)
{
	(void)arg;
	/* Mirror how hello_ll would be launched on Linux: -f (foreground),
	 * -s (single-threaded), mountpoint "/" */
	char *bk_argv[] = { "fuse", "-f", "-s", "/" };
	fuse_backend_main(4, bk_argv);
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

	printf("\n========================================\n");
	printf("  FUSE-UKFS M4: LOOKUP & GETATTR Tests\n");
	printf("========================================\n\n");

	/* Start the FUSE backend in its own thread; it will call
	 * fuse_session_new + fuse_session_mount + fuse_session_loop (blocks). */
	struct uk_alloc *a = uk_alloc_get_default();
	struct uk_thread *bt = uk_thread_create_fn1(a,
						     backend_thread_fn, NULL,
						     a, 32768,
						     NULL, 0,
						     a, false,
						     "fuse-backend",
						     NULL, NULL);
	UK_ASSERT(bt != NULL);
	uk_sched_thread_add(uk_sched_current(), bt);

	/* Yield until fuse_session_mount has registered the session */
	while (fuse_session_get_registered() == NULL)
		uk_sched_yield();

	/* Mount the filesystem; this triggers fusefs_vopen which initialises
	 * the queue and auto-starts the daemon using the registered session. */
	printf("[TRACE] About to mount fuse at /...\n");
	ret = mount("none", "/", "fuse", 0, NULL);
	printf("[TRACE] mount returned %d (errno=%d)\n", ret, errno);
	UK_ASSERT(ret == 0);
	printf("[+] FUSE mounted at / (daemon started automatically)\n");

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
