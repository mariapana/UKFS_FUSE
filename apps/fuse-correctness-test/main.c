/*
 * UKFS-FUSE Correctness Test Suite
 *
 * Tests are derived from three industry-standard sources:
 *
 *  [LIBFUSE] libfuse/test/test_syscalls.c
 *            https://github.com/libfuse/libfuse/blob/master/test/test_syscalls.c
 *            Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
 *            Licensed under GPL-2.0
 *
 *  [PJDFSTEST] pjd/pjdfstest - POSIX filesystem conformance suite
 *              https://github.com/pjd/pjdfstest
 *              Copyright (c) 2006-2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 *              Licensed under BSD-2-Clause
 *
 *  [LTP] Linux Test Project - kernel syscall test suite
 *        https://github.com/linux-test-project/ltp
 *        Copyright (c) IBM Corp., Linux Test Project contributors
 *        Licensed under GPL-2.0-or-later
 *        Specific files referenced:
 *          testcases/kernel/syscalls/lseek/lseek01.c
 *          testcases/kernel/syscalls/pread/pread01.c
 *          testcases/kernel/syscalls/access/access01.c
 *          testcases/kernel/syscalls/read/read01.c
 *
 * Only read-only test cases are included here, as the hello_ll backend
 * exposes a read-only filesystem with one directory (/) and one file (/hello).
 *
 * Backend invariants (hello_ll):
 *   /        ino=1  mode=S_IFDIR|0755  nlink=2
 *   /hello   ino=2  mode=S_IFREG|0444  nlink=1  size=13  content="Hello World!\n"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <uk/assert.h>

/* access() mode bits - may not be exposed by nolibc */
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#include <fuse_lowlevel.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>

/* hello_ll.c is compiled with -Dmain=fuse_backend_main */
extern int fuse_backend_main(int argc, char *argv[]);

static void backend_thread_fn(void *arg)
{
	(void)arg;
	char *bk_argv[] = { "fuse", "-f", "-s", "/" };
	fuse_backend_main(4, bk_argv);
}

/* -------------------------------------------------------------------------
 * Test infrastructure
 * Modelled after the testname/PASS/FAIL pattern in [LIBFUSE] test_syscalls.c
 * ------------------------------------------------------------------------- */

static unsigned int test_passed = 0;
static unsigned int test_failed = 0;

#define TEST_PASS(name) \
	do { printf("[PASS] %s\n", (name)); test_passed++; } while (0)

#define TEST_FAIL(name, fmt, ...) \
	do { printf("[FAIL] %s: " fmt "\n", (name), ##__VA_ARGS__); \
	     test_failed++; } while (0)

/* -------------------------------------------------------------------------
 * Helper: check_type
 * [LIBFUSE] test_syscalls.c - st_check_type / check_type pattern
 * Verifies the file type bits of st_mode via lstat().
 * ------------------------------------------------------------------------- */
static int check_type(const char *path, mode_t expected_type)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return -1;
	if ((st.st_mode & S_IFMT) != expected_type)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_mode
 * [LIBFUSE] test_syscalls.c - st_check_mode / check_mode pattern
 * Verifies the permission bits of st_mode.
 * ------------------------------------------------------------------------- */
static int check_mode(const char *path, mode_t expected_perm)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return -1;
	if ((st.st_mode & 0777) != expected_perm)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_size
 * [LIBFUSE] test_syscalls.c - st_check_size / check_size pattern
 * Verifies st_size via stat().
 * ------------------------------------------------------------------------- */
static int check_size(const char *path, off_t expected_size)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return -1;
	if (st.st_size != expected_size)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_nlink
 * [PJDFSTEST] pjdfstest.c - ACTION_STAT / show_stat "nlink" field check
 * Verifies st_nlink via stat().
 * ------------------------------------------------------------------------- */
static int check_nlink(const char *path, nlink_t expected)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return -1;
	if (st.st_nlink != expected)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_ino
 * [PJDFSTEST] pjdfstest.c - ACTION_STAT / show_stat "inode" field check
 * Verifies st_ino via stat().
 * ------------------------------------------------------------------------- */
static int check_ino(const char *path, ino_t expected)
{
	struct stat st;
	if (stat(path, &st) == -1)
		return -1;
	if (st.st_ino != expected)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_data
 * [LIBFUSE] test_syscalls.c - check_data() function
 * Opens the file, seeks to offset, reads len bytes, compares to data.
 * ------------------------------------------------------------------------- */
static int check_data(const char *path, const char *data,
		      off_t offset, unsigned int len)
{
	char buf[256];
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
		close(fd);
		return -1;
	}
	ssize_t n = read(fd, buf, len);
	close(fd);
	if (n != (ssize_t)len)
		return -1;
	if (memcmp(buf, data, len) != 0)
		return -1;
	return 0;
}

/* -------------------------------------------------------------------------
 * Helper: check_nonexist
 * [LIBFUSE] test_syscalls.c - check_nonexist() function
 * Asserts that stat() returns ENOENT for the given path.
 * ------------------------------------------------------------------------- */
static int check_nonexist(const char *path)
{
	struct stat st;
	int res = stat(path, &st);
	if (res == 0)
		return -1; /* file exists, should not */
	if (errno != ENOENT)
		return -1; /* wrong error */
	return 0;
}

/* -------------------------------------------------------------------------
 * T1: stat("/") - root directory attributes
 * [PJDFSTEST] ACTION_STAT: verifies mode, type, nlink, ino fields
 * [LIBFUSE]   test_syscalls.c: check_type / check_mode pattern
 * ------------------------------------------------------------------------- */
static void test_stat_root(void)
{
	const char *name = "T1: stat(\"/\") - root dir attributes";
	struct stat st;

	if (stat("/", &st) != 0) {
		TEST_FAIL(name, "stat failed: errno=%d", errno);
		return;
	}
	if (!S_ISDIR(st.st_mode)) {
		TEST_FAIL(name, "not a directory: st_mode=0%o", st.st_mode);
		return;
	}
	/* [PJDFSTEST] inode check */
	if (st.st_ino != 1) {
		TEST_FAIL(name, "st_ino=%llu expected 1",
			  (unsigned long long)st.st_ino);
		return;
	}
	/* [PJDFSTEST] nlink check: directories have nlink >= 2 */
	if (st.st_nlink < 2) {
		TEST_FAIL(name, "st_nlink=%lu expected >=2",
			  (unsigned long)st.st_nlink);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T2: stat("/hello") - regular file attributes (full field check)
 * [PJDFSTEST] ACTION_STAT: verifies type, mode, ino, nlink, size fields
 * [LIBFUSE]   check_type + check_mode + check_size pattern
 * ------------------------------------------------------------------------- */
static void test_stat_file(void)
{
	const char *name = "T2: stat(\"/hello\") - file attributes";
	struct stat st;

	if (stat("/hello", &st) != 0) {
		TEST_FAIL(name, "stat failed: errno=%d", errno);
		return;
	}
	/* [LIBFUSE] check_type */
	if (!S_ISREG(st.st_mode)) {
		TEST_FAIL(name, "not a regular file: st_mode=0%o", st.st_mode);
		return;
	}
	/* [LIBFUSE] check_mode */
	if ((st.st_mode & 0777) != 0444) {
		TEST_FAIL(name, "mode=0%o expected 0444",
			  (unsigned int)(st.st_mode & 0777));
		return;
	}
	/* [PJDFSTEST] inode check */
	if (st.st_ino != 2) {
		TEST_FAIL(name, "st_ino=%llu expected 2",
			  (unsigned long long)st.st_ino);
		return;
	}
	/* [PJDFSTEST] nlink check */
	if (st.st_nlink != 1) {
		TEST_FAIL(name, "st_nlink=%lu expected 1",
			  (unsigned long)st.st_nlink);
		return;
	}
	/* [LIBFUSE] check_size */
	if (st.st_size != 13) {
		TEST_FAIL(name, "st_size=%lld expected 13",
			  (long long)st.st_size);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T3: stat on non-existent path must return ENOENT
 * [PJDFSTEST] open() / stat() with expected ENOENT error code
 * [LIBFUSE]   check_nonexist() pattern
 * ------------------------------------------------------------------------- */
static void test_stat_enoent(void)
{
	const char *name = "T3: stat(\"/nonexistent\") - ENOENT";

	if (check_nonexist("/nonexistent") != 0) {
		TEST_FAIL(name, "expected ENOENT, got errno=%d or file exists",
			  errno);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T4: fstat() on an open fd must return the same fields as stat()
 * [LIBFUSE]   test_syscalls.c - fcheck_stat() pattern
 * [PJDFSTEST] ACTION_FSTAT
 * ------------------------------------------------------------------------- */
static void test_fstat_file(void)
{
	const char *name = "T4: fstat(\"/hello\") - matches stat()";
	struct stat st_path, st_fd;

	if (stat("/hello", &st_path) != 0) {
		TEST_FAIL(name, "stat failed: errno=%d", errno);
		return;
	}
	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		TEST_FAIL(name, "open failed: errno=%d", errno);
		return;
	}
	if (fstat(fd, &st_fd) != 0) {
		TEST_FAIL(name, "fstat failed: errno=%d", errno);
		close(fd);
		return;
	}
	close(fd);

	if (st_fd.st_ino != st_path.st_ino ||
	    st_fd.st_mode != st_path.st_mode ||
	    st_fd.st_size != st_path.st_size ||
	    st_fd.st_nlink != st_path.st_nlink) {
		TEST_FAIL(name, "fstat and stat disagree");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T5: open(O_RDONLY) on non-existent file must return ENOENT
 * [PJDFSTEST] ACTION_OPEN: open() with expected error ENOENT
 * [LIBFUSE]   do_test_open() - !exist && !(flags & O_CREAT) path
 * ------------------------------------------------------------------------- */
static void test_open_enoent(void)
{
	const char *name = "T5: open(\"/nonexistent\", O_RDONLY) - ENOENT";

	int fd = open("/nonexistent", O_RDONLY);
	if (fd >= 0) {
		TEST_FAIL(name, "open succeeded unexpectedly");
		close(fd);
		return;
	}
	if (errno != ENOENT) {
		TEST_FAIL(name, "errno=%d expected ENOENT", errno);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T6: open(O_RDONLY) on a directory succeeds (type check via fstat)
 * [LIBFUSE]   do_test_open() - check_type after open
 * [PJDFSTEST] open() + fstat type verification
 * ------------------------------------------------------------------------- */
static void test_open_dir(void)
{
	const char *name = "T6: open(\"/\", O_RDONLY) - directory opens";
	struct stat st;

	int fd = open("/", O_RDONLY);
	if (fd < 0) {
		TEST_FAIL(name, "open failed: errno=%d", errno);
		return;
	}
	if (fstat(fd, &st) != 0) {
		TEST_FAIL(name, "fstat failed: errno=%d", errno);
		close(fd);
		return;
	}
	close(fd);
	if (!S_ISDIR(st.st_mode)) {
		TEST_FAIL(name, "fstat: not a directory, mode=0%o", st.st_mode);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T7: read full file content
 * [LIBFUSE]   test_syscalls.c - check_data() with offset=0, len=full
 * [PJDFSTEST] ACTION_PREAD: pread content verification
 * ------------------------------------------------------------------------- */
static void test_read_full(void)
{
	const char *name = "T7: read(\"/hello\") - full content correct";
	const char *expected = "Hello World!\n";

	if (check_data("/hello", expected, 0, 13) != 0) {
		TEST_FAIL(name, "data mismatch or read error");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T8: read with non-zero offset (lseek + read)
 * [LIBFUSE]   test_syscalls.c - check_data() with offset parameter
 *             fcheck_data() with lseek to arbitrary offset
 * ------------------------------------------------------------------------- */
static void test_read_offset(void)
{
	const char *name = "T8: read(\"/hello\", offset=7) - partial content";

	/* "Hello World!\n" from offset 7 = "orld!\n" (6 bytes) */
	if (check_data("/hello", "orld!\n", 7, 6) != 0) {
		TEST_FAIL(name, "data mismatch at offset 7");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T9: read at EOF must return 0 bytes
 * [LIBFUSE]   test_syscalls.c - read returning 0 at end of file
 * Modelled after the EOF check in check_data() / do_test_open()
 * ------------------------------------------------------------------------- */
static void test_read_eof(void)
{
	const char *name = "T9: read at EOF - returns 0";
	char buf[16];

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		TEST_FAIL(name, "open failed: errno=%d", errno);
		return;
	}
	/* Seek past end of file (size=13) */
	if (lseek(fd, 100, SEEK_SET) == (off_t)-1) {
		TEST_FAIL(name, "lseek failed: errno=%d", errno);
		close(fd);
		return;
	}
	ssize_t n = read(fd, buf, sizeof(buf));
	close(fd);
	if (n != 0) {
		TEST_FAIL(name, "read returned %ld, expected 0", (long)n);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T10: read partial (size smaller than file)
 * [LIBFUSE]   test_syscalls.c - short read / rdlen clamping in check_data()
 * ------------------------------------------------------------------------- */
static void test_read_partial(void)
{
	const char *name = "T10: read(\"/hello\", size=5) - partial read";

	if (check_data("/hello", "Hello", 0, 5) != 0) {
		TEST_FAIL(name, "first 5 bytes mismatch");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T11: sequential reads advance the file offset correctly
 * [LIBFUSE]   test_syscalls.c - fcheck_data() with sequential reads
 * Verifies that two consecutive read() calls advance offset automatically.
 * ------------------------------------------------------------------------- */
static void test_read_sequential(void)
{
	const char *name = "T11: sequential reads - offset advances";
	char buf1[6], buf2[8];

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		TEST_FAIL(name, "open failed: errno=%d", errno);
		return;
	}

	ssize_t n1 = read(fd, buf1, 5); /* "Hello" */
	ssize_t n2 = read(fd, buf2, 8); /* " World!\n" */
	close(fd);

	if (n1 != 5) {
		TEST_FAIL(name, "first read returned %ld, expected 5", (long)n1);
		return;
	}
	if (n2 != 8) {
		TEST_FAIL(name, "second read returned %ld, expected 8", (long)n2);
		return;
	}
	if (memcmp(buf1, "Hello", 5) != 0) {
		TEST_FAIL(name, "first chunk mismatch");
		return;
	}
	if (memcmp(buf2, " World!\n", 8) != 0) {
		TEST_FAIL(name, "second chunk mismatch");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T12: readdir - all expected entries present, no duplicates
 * [LIBFUSE]   test_syscalls.c - check_dir_contents() function
 *             Iterates readdir loop, checks each d_name against expected list,
 *             reports missing or unexpected entries.
 * [PJDFSTEST] readdir correctness is implied by ACTION_OPEN on directories.
 * ------------------------------------------------------------------------- */
static void test_readdir_contents(void)
{
	const char *name = "T12: readdir(\"/\") - all entries present";

	/* Expected non-dot entries in the root directory */
	const char *expected[] = { "hello", NULL };
	int found_hello = 0;
	int unexpected  = 0;

	DIR *dp = opendir("/");
	if (!dp) {
		TEST_FAIL(name, "opendir failed: errno=%d", errno);
		return;
	}

	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		/* [LIBFUSE] is_dot_or_dotdot pattern: skip . and .. */
		if (de->d_name[0] == '.' &&
		    (de->d_name[1] == '\0' ||
		     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;

		if (strcmp(de->d_name, "hello") == 0) {
			found_hello++;
		} else {
			printf("  [unexpected entry: %s]\n", de->d_name);
			unexpected++;
		}
	}
	closedir(dp);

	if (!found_hello) {
		TEST_FAIL(name, "entry 'hello' not found");
		return;
	}
	if (found_hello > 1) {
		TEST_FAIL(name, "duplicate entry 'hello'");
		return;
	}
	if (unexpected) {
		TEST_FAIL(name, "%d unexpected entries", unexpected);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T13: readdir - d_ino field correctness
 * [PJDFSTEST] ACTION_STAT / show_stat "inode" - verifies inode numbers
 * [LIBFUSE]   readdir_inode.c test - d_ino must match stat().st_ino
 * ------------------------------------------------------------------------- */
static void test_readdir_ino(void)
{
	const char *name = "T13: readdir(\"/\") - d_ino matches stat()";

	DIR *dp = opendir("/");
	if (!dp) {
		TEST_FAIL(name, "opendir failed: errno=%d", errno);
		return;
	}

	int err = 0;
	struct dirent *de;
	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		char path[64];

		if (de->d_name[0] == '.' &&
		    (de->d_name[1] == '\0' ||
		     (de->d_name[1] == '.' && de->d_name[2] == '\0')))
			continue;

		snprintf(path, sizeof(path), "/%s", de->d_name);
		if (stat(path, &st) != 0) {
			printf("  [stat failed for %s]\n", path);
			err++;
			continue;
		}
		if ((ino_t)de->d_ino != st.st_ino) {
			printf("  [%s: d_ino=%llu != st_ino=%llu]\n", de->d_name,
			       (unsigned long long)de->d_ino,
			       (unsigned long long)st.st_ino);
			err++;
		}
	}
	closedir(dp);

	if (err) {
		TEST_FAIL(name, "%d inode mismatches", err);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T14: repeated opendir/closedir - no crash, no memory corruption
 * [LIBFUSE]   test_syscalls.c - repeated test pattern with cleanup
 * Verifies reference counting robustness (upref in fusefs_release).
 * ------------------------------------------------------------------------- */
static void test_readdir_repeated(void)
{
	const char *name = "T14: opendir/closedir x3 - no crash";

	for (int i = 0; i < 3; i++) {
		DIR *dp = opendir("/");
		if (!dp) {
			TEST_FAIL(name, "iteration %d: opendir failed: errno=%d",
				  i, errno);
			return;
		}
		while (readdir(dp) != NULL)
			;
		closedir(dp);
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T24: two open fds on the same file - independent file positions
 * Verifies that fh and the VFS file offset are per-fusefs_file, not
 * per-inode. Opening the same path twice creates two independent
 * fusefs_file structs; seeking one must not affect the other.
 * ------------------------------------------------------------------------- */
static void test_two_fds(void)
{
	const char *name = "T24: two fds on \"/hello\" - independent positions";
	char buf1[8] = {0}, buf2[8] = {0};

	int fd1 = open("/hello", O_RDONLY);
	if (fd1 < 0) {
		TEST_FAIL(name, "fd1 open failed: errno=%d", errno);
		return;
	}
	int fd2 = open("/hello", O_RDONLY);
	if (fd2 < 0) {
		TEST_FAIL(name, "fd2 open failed: errno=%d", errno);
		close(fd1);
		return;
	}

	/* Advance fd1 to offset 7 ("orld!\n" starts here) */
	if (lseek(fd1, 7, SEEK_SET) != 7) {
		TEST_FAIL(name, "lseek fd1 failed: errno=%d", errno);
		close(fd1); close(fd2);
		return;
	}

	/* fd2 must still be at offset 0 → reads "Hello" */
	ssize_t n2 = read(fd2, buf2, 5);
	/* fd1 must read from offset 7 → reads "orld!" */
	ssize_t n1 = read(fd1, buf1, 5);

	close(fd1);
	close(fd2);

	if (n2 != 5 || memcmp(buf2, "Hello", 5) != 0) {
		TEST_FAIL(name, "fd2 wrong: expected 'Hello' got '%.5s'", buf2);
		return;
	}
	if (n1 != 5 || memcmp(buf1, "orld!", 5) != 0) {
		TEST_FAIL(name, "fd1 wrong: expected 'orld!' got '%.5s'", buf1);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T15: stat() after open+read - file state not corrupted by lazy-open
 * [LIBFUSE]   test_syscalls.c - state consistency check across operations
 * Verifies that lazy-open (fh population) does not corrupt subsequent stat().
 * ------------------------------------------------------------------------- */
static void test_stat_after_read(void)
{
	const char *name = "T15: stat(\"/hello\") after read - consistent";
	char buf[16];

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		TEST_FAIL(name, "open failed: errno=%d", errno);
		return;
	}
	(void)read(fd, buf, 5); /* trigger lazy-open; fh now valid */
	close(fd);

	/* stat() must still report correct attributes */
	if (check_type("/hello", S_IFREG) != 0 ||
	    check_size("/hello", 13) != 0 ||
	    check_ino("/hello", 2) != 0) {
		TEST_FAIL(name, "stat inconsistent after read");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T16: stat("/hello") multiple times - idempotent
 * [PJDFSTEST] pjdfstest verifies that repeated stat() returns stable fields
 * ------------------------------------------------------------------------- */
static void test_stat_idempotent(void)
{
	const char *name = "T16: stat(\"/hello\") x3 - idempotent";
	struct stat st[3];

	for (int i = 0; i < 3; i++) {
		if (stat("/hello", &st[i]) != 0) {
			TEST_FAIL(name, "stat [%d] failed: errno=%d", i, errno);
			return;
		}
	}
	if (st[0].st_ino != st[1].st_ino || st[1].st_ino != st[2].st_ino ||
	    st[0].st_size != st[1].st_size || st[1].st_size != st[2].st_size ||
	    st[0].st_mode != st[1].st_mode || st[1].st_mode != st[2].st_mode) {
		TEST_FAIL(name, "stat returned different results across calls");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T17: lseek SEEK_SET - position to absolute offset, then read
 * [LTP] lseek01.c tcase {4, SEEK_SET, "SEEK_SET", 4, 3, "efg"} adapted
 *       for hello_ll content "Hello World!\n" (len=13)
 *       SEEK_SET to offset 6 → read 6 bytes → "orld!\n"
 * ------------------------------------------------------------------------- */
static void test_lseek_seek_set(void)
{
	const char *name = "T17: lseek(SEEK_SET, 6) + read - correct data";
	char buf[16] = {0};

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	off_t pos = lseek(fd, 6, SEEK_SET);
	if (pos != 6) {
		TEST_FAIL(name, "lseek returned %lld expected 6", (long long)pos);
		close(fd); return;
	}
	ssize_t n = read(fd, buf, 6);
	close(fd);
	if (n != 6 || memcmp(buf, "World!", 6) != 0) {
		TEST_FAIL(name, "data mismatch after SEEK_SET");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T18: lseek SEEK_CUR - relative seek from current position
 * [LTP] lseek01.c tcase {-2, SEEK_CUR, "SEEK_CUR", ...} pattern
 *       Read 5 bytes first ("Hello"), then SEEK_CUR +3 → at offset 8
 *       Read 5 bytes → "rld!\n"
 * ------------------------------------------------------------------------- */
static void test_lseek_seek_cur(void)
{
	const char *name = "T18: lseek(SEEK_CUR, +3) - relative seek correct";
	char buf[16] = {0};

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	/* advance to offset 5 by reading "Hello" */
	char discard[5];
	(void)read(fd, discard, 5);

	off_t pos = lseek(fd, 3, SEEK_CUR);
	if (pos != 8) {
		TEST_FAIL(name, "lseek returned %lld expected 8", (long long)pos);
		close(fd); return;
	}
	ssize_t n = read(fd, buf, 5);
	close(fd);
	if (n != 5 || memcmp(buf, "rld!\n", 5) != 0) {
		TEST_FAIL(name, "data mismatch after SEEK_CUR");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T19: lseek SEEK_END - seek relative to end of file
 * [LTP] lseek01.c tcase {-4, SEEK_END, "SEEK_END", ...} pattern
 *       SEEK_END with offset -5 → position at 13-5=8 → read "rld!\n"
 * ------------------------------------------------------------------------- */
static void test_lseek_seek_end(void)
{
	const char *name = "T19: lseek(SEEK_END, -5) - seek from end correct";
	char buf[16] = {0};

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	off_t pos = lseek(fd, -5, SEEK_END);
	if (pos != 8) {
		TEST_FAIL(name, "lseek returned %lld expected 8", (long long)pos);
		close(fd); return;
	}
	ssize_t n = read(fd, buf, 5);
	close(fd);
	if (n != 5 || memcmp(buf, "rld!\n", 5) != 0) {
		TEST_FAIL(name, "data mismatch after SEEK_END");
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T20: lseek SEEK_END to file size - returns file length
 * [LTP] lseek01.c tcase {0, SEEK_END, "SEEK_END", 7, 0, NULL} pattern
 *       lseek(fd, 0, SEEK_END) must return st_size (13 for /hello)
 * ------------------------------------------------------------------------- */
static void test_lseek_seek_end_size(void)
{
	const char *name = "T20: lseek(SEEK_END, 0) == file size (13)";

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	off_t pos = lseek(fd, 0, SEEK_END);
	close(fd);
	if (pos != 13) {
		TEST_FAIL(name, "SEEK_END returned %lld expected 13", (long long)pos);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T21: pread - positional read does not advance file offset
 * [LTP] pread01.c core invariant:
 *       pread(fd, buf, n, off) reads from 'off' without changing the
 *       current file position. Verified by checking SEEK_CUR after pread.
 * ------------------------------------------------------------------------- */
static void test_pread_no_offset_change(void)
{
	const char *name = "T21: pread(offset=7) - file offset unchanged";
	char buf[8] = {0};

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	/* position at offset 3 */
	lseek(fd, 3, SEEK_SET);

	/* pread from offset 7 - must not move the current position */
	ssize_t n = pread(fd, buf, 6, 7);
	if (n != 6 || memcmp(buf, "orld!\n", 6) != 0) {
		TEST_FAIL(name, "pread returned wrong data");
		close(fd); return;
	}

	/* current offset must still be 3 */
	off_t cur = lseek(fd, 0, SEEK_CUR);
	close(fd);
	if (cur != 3) {
		TEST_FAIL(name, "file offset moved to %lld after pread, expected 3",
			  (long long)cur);
		return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T22: pread at multiple offsets - data correct at each
 * [LTP] pread01.c: compare_bufers() pattern - reads at different offsets
 *       and verifies content independently of sequential state.
 * ------------------------------------------------------------------------- */
static void test_pread_multiple_offsets(void)
{
	const char *name = "T22: pread at offsets 0, 6, 12 - data correct";
	char b0[6], b6[6], b12[2];

	int fd = open("/hello", O_RDONLY);
	if (fd < 0) { TEST_FAIL(name, "open failed: errno=%d", errno); return; }

	ssize_t n0  = pread(fd, b0,  5, 0);
	ssize_t n6  = pread(fd, b6,  6, 6);
	ssize_t n12 = pread(fd, b12, 1, 12);
	close(fd);

	if (n0 != 5 || memcmp(b0, "Hello", 5) != 0) {
		TEST_FAIL(name, "pread(offset=0) mismatch"); return;
	}
	if (n6 != 6 || memcmp(b6, "World!", 6) != 0) {
		TEST_FAIL(name, "pread(offset=6) mismatch"); return;
	}
	if (n12 != 1 || b12[0] != '\n') {
		TEST_FAIL(name, "pread(offset=12) mismatch"); return;
	}
	TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * T23: access() - F_OK and R_OK on existing file/dir, ENOENT on missing
 * [LTP] access01.c: basic F_OK / R_OK / W_OK tcase pattern
 *       /hello has mode 0444 → R_OK must pass, W_OK must fail with EACCES
 *       /      has mode 0755 → R_OK and F_OK must pass
 *       /nonexistent         → F_OK must fail with ENOENT
 * ------------------------------------------------------------------------- */
static void test_access(void)
{
	const char *name = "T23: access() - F_OK, R_OK, ENOENT";
	int err = 0;

	/* [LTP] access01.c: F_OK - file exists */
	if (access("/hello", F_OK) != 0) {
		printf("  [FAIL] access /hello F_OK: errno=%d\n", errno);
		err++;
	}
	/* [LTP] access01.c: R_OK - file is readable (mode 0444) */
	if (access("/hello", R_OK) != 0) {
		printf("  [FAIL] access /hello R_OK: errno=%d\n", errno);
		err++;
	}
	/* W_OK omitted: unikernel always runs as uid=0 (root), which bypasses
	 * mode-bit write restrictions — access(W_OK) returns 0 even for mode
	 * 0444. Correct POSIX behaviour, but untestable in a uid=0 environment. */
	/* [LTP] access01.c: F_OK on directory */
	if (access("/", F_OK) != 0) {
		printf("  [FAIL] access / F_OK: errno=%d\n", errno);
		err++;
	}
	/* [LTP] access01.c: F_OK on non-existent path → ENOENT */
	if (access("/nonexistent", F_OK) == 0 || errno != ENOENT) {
		printf("  [FAIL] access /nonexistent F_OK should give ENOENT\n");
		err++;
	}
	if (err)
		TEST_FAIL(name, "%d sub-checks failed", err);
	else
		TEST_PASS(name);
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(int argc __unused, char *argv[] __unused)
{
	printf("\n");
	printf("=====================================================\n");
	printf("  UKFS-FUSE Correctness Test Suite\n");
	printf("  Sources: libfuse/test_syscalls.c, pjdfstest, LTP\n");
	printf("=====================================================\n\n");

	/* Start the FUSE backend in its own thread */
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

	/* Wait until fuse_session_mount has registered the session */
	while (fuse_session_get_registered() == NULL)
		uk_sched_yield();

	int ret = mount("none", "/", "fuse", 0, NULL);
	if (ret != 0) {
		printf("FATAL: mount failed errno=%d\n", errno);
		return 1;
	}
	printf("[+] FUSE filesystem mounted at /\n\n");

	/* --- Run all tests --- */
	test_stat_root();
	test_stat_file();
	test_stat_enoent();
	test_fstat_file();
	test_open_enoent();
	test_open_dir();
	test_read_full();
	test_read_offset();
	test_read_eof();
	test_read_partial();
	test_read_sequential();
	test_readdir_contents();
	test_readdir_ino();
	test_readdir_repeated();
	test_two_fds();
	test_stat_after_read();
	test_stat_idempotent();
	/* [LTP] tests */
	test_lseek_seek_set();
	test_lseek_seek_cur();
	test_lseek_seek_end();
	test_lseek_seek_end_size();
	test_pread_no_offset_change();
	test_pread_multiple_offsets();
	test_access();

	/* --- Summary --- */
	printf("\n=====================================================\n");
	printf("  Results: %u passed, %u failed\n",
	       test_passed, test_failed);
	printf("=====================================================\n");

	if (test_failed > 0) {
		printf("CORRECTNESS TEST SUITE: FAILED\n");
		return 1;
	}
	printf("CORRECTNESS TEST SUITE: ALL PASSED\n");
	return 0;
}
