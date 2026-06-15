/*
 * UKFS-FUSE Performance Test Suite
 *
 * A statistical benchmarking harness that uses RDTSC to measure
 * tail-latencies of FUSE operations in a Unikraft unikernel.
 *
 * This serves as the Unikraft baseline for comparison against
 * Linux FUSE native benchmarks (e.g. FIO).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)
#include <fuse_lowlevel.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>

/* Number of iterations for statistical significance */
#define NUM_ITERATIONS 100000

/* Global arrays for storing latency samples */
static uint64_t samples[NUM_ITERATIONS];

/* RDTSC helper for cycle-accurate profiling */
static inline uint64_t rdtsc(void) {
	uint32_t lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
}

/* Sorting helper for percentiles */
static int cmp_u64(const void *a, const void *b) {
	uint64_t arg1 = *(const uint64_t *)a;
	uint64_t arg2 = *(const uint64_t *)b;
	if (arg1 < arg2) return -1;
	if (arg1 > arg2) return 1;
	return 0;
}

/* Statistical reporter */
static void print_stats(const char *name, uint64_t *times, int count) {
	qsort(times, count, sizeof(uint64_t), cmp_u64);
	uint64_t min = times[0];
	uint64_t max = times[count - 1];
	uint64_t p50 = times[count / 2];
	uint64_t p90 = times[(count * 90) / 100];
	uint64_t p95 = times[(count * 95) / 100];
	uint64_t p99 = times[(count * 99) / 100];
	uint64_t p999 = times[(count * 999) / 1000];
	
	uint64_t sum = 0;
	for (int i = 0; i < count; i++) sum += times[i];
	uint64_t avg = sum / count;

	printf("\n=====================================================\n");
	printf("  Workload: %s\n", name);
	printf("=====================================================\n");
	printf("  Samples: %d\n", count);
	printf("  Avg:     %llu cycles\n", (unsigned long long)avg);
	printf("  Min:     %llu cycles\n", (unsigned long long)min);
	printf("  p50:     %llu cycles\n", (unsigned long long)p50);
	printf("  p90:     %llu cycles\n", (unsigned long long)p90);
	printf("  p95:     %llu cycles\n", (unsigned long long)p95);
	printf("  p99:     %llu cycles\n", (unsigned long long)p99);
	printf("  p99.9:   %llu cycles\n", (unsigned long long)p999);
	printf("  Max:     %llu cycles\n", (unsigned long long)max);
	printf("-----------------------------------------------------\n");
}

/* -------------------------------------------------------------------------
 * Benchmark 1: Metadata Storm (stat)
 * Tests LOOKUP + GETATTR in a tight loop.
 * ------------------------------------------------------------------------- */
static void bench_stat(void) {
	struct stat st;
	
	/* Warmup */
	for (int i = 0; i < 100; i++) stat("/hello", &st);
	
	/* Benchmark */
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		stat("/hello", &st);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	
	print_stats("stat(\"/hello\") - LOOKUP+GETATTR", samples, NUM_ITERATIONS);
}

/* -------------------------------------------------------------------------
 * Benchmark 1b: stat("/") - GETATTR only (1 round-trip)
 * Isolates single FUSE round-trip cost; compare with bench_stat() (2 RT).
 * ------------------------------------------------------------------------- */
static void bench_stat_root(void) {
	struct stat st;

	/* Warmup */
	for (int i = 0; i < 100; i++) stat("/", &st);

	/* Benchmark */
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		stat("/", &st);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("stat(\"/\") - GETATTR only (1 round-trip)", samples, NUM_ITERATIONS);
}

/* -------------------------------------------------------------------------
 * Benchmark 2: Positional Read (pread)
 * Tests I/O path (lazy-open is done once)
 * ------------------------------------------------------------------------- */
static void bench_pread(void) {
	char buf[16];
	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		printf("Failed to open /hello for pread bench\n");
		return;
	}
	
	/* Warmup */
	for (int i = 0; i < 100; i++) pread(fd, buf, 13, 0);
	
	/* Benchmark */
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		pread(fd, buf, 13, 0);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	
	close(fd);
	print_stats("pread(\"/hello\", 13, 0) - Data Transfer", samples, NUM_ITERATIONS);
}

/* -------------------------------------------------------------------------
 * Benchmark 2c: Sequential read - two consecutive read() calls
 * lseek(0) resets position (pure VFS, no FUSE round-trip), then two
 * read() calls each trigger one FUSE READ. Expected: ~2x bench_pread().
 * Verifies that file-position tracking adds no measurable overhead.
 * ------------------------------------------------------------------------- */
static void bench_seq_read(void) {
	char buf[16];
	int fd = open("/hello", O_RDONLY);
	if (fd < 0) {
		printf("Failed to open /hello for seq read bench\n");
		return;
	}

	/* Warmup */
	for (int i = 0; i < 100; i++) {
		lseek(fd, 0, SEEK_SET);
		read(fd, buf, 5);
		read(fd, buf, 8);
	}

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		lseek(fd, 0, SEEK_SET);  /* pure VFS, outside timer */
		uint64_t t0 = rdtsc();
		read(fd, buf, 5);
		read(fd, buf, 8);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	close(fd);
	print_stats("seq read(\"/hello\") 5+8 bytes - 2 FUSE READs", samples, NUM_ITERATIONS);
}

/* -------------------------------------------------------------------------
 * Benchmark 2b: open + pread + close - includes lazy FUSE OPEN
 * Each iteration creates a new fd, so fusefs_do_open fires on first pread.
 * Compare with bench_pread() (steady-state, OPEN already done).
 * Fewer iterations: each involves open + FUSE OPEN + FUSE READ + close.
 * ------------------------------------------------------------------------- */
static void bench_open_read_close(void) {
	char buf[16];
	const int count = 1000;

	for (int i = 0; i < count; i++) {
		uint64_t t0 = rdtsc();
		int fd = open("/hello", O_RDONLY);
		if (fd >= 0) {
			pread(fd, buf, 13, 0);
			close(fd);
		}
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("open+pread+close (\"/hello\") - incl. lazy FUSE OPEN",
		    samples, count);
}

/* -------------------------------------------------------------------------
 * Benchmark 3: Directory Traversal (opendir + readdir)
 * Tests complex stateful operations.
 * ------------------------------------------------------------------------- */
static void bench_readdir(void) {
	/* Reduce iterations for readdir as it's a multi-syscall compound */
	int count = NUM_ITERATIONS / 10;

	/* Warmup */
	for (int i = 0; i < 20; i++) {
		DIR *dp = opendir("/");
		if (dp) { while (readdir(dp)) {} closedir(dp); }
	}

	/* Benchmark */
	for (int i = 0; i < count; i++) {
		uint64_t t0 = rdtsc();
		DIR *dp = opendir("/");
		if (dp) {
			while (readdir(dp)) { }
			closedir(dp);
		}
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	
	print_stats("opendir + readdir + closedir", samples, count);
}

/* -------------------------------------------------------------------------
 * Benchmark 4: Bandwidth Throughput (MB/s)
 * Tests raw sequential memory transfer speeds over FUSE
 * ------------------------------------------------------------------------- */
static void bench_throughput(size_t block_size) {
	int fd = open("/bigfile", O_RDONLY);
	if (fd < 0) {
		printf("Failed to open /bigfile for throughput bench\n");
		return;
	}

	char *buf = malloc(block_size);
	if (!buf) {
		close(fd);
		return;
	}

	/* We will read a total of 50 MB */
	size_t target_bytes = 50 * 1024 * 1024;
	size_t bytes_read = 0;
	ssize_t n;

	uint64_t t0 = rdtsc();
	while (bytes_read < target_bytes) {
		n = read(fd, buf, block_size);
		if (n <= 0) break;
		bytes_read += n;
	}
	uint64_t t1 = rdtsc();
	
	close(fd);
	free(buf);

	uint64_t total_cycles = t1 - t0;
	uint32_t mb = (uint32_t)(bytes_read / (1024 * 1024));
	/* cycles/byte is TSC-frequency-independent and directly comparable */
	uint64_t cycles_per_byte = bytes_read > 0 ? total_cycles / bytes_read : 0;
	/* Approximate MB/s: assumes 3 GHz TSC — adjust for actual host frequency */
	uint64_t ms_approx = total_cycles / 3000000;
	if (ms_approx == 0) ms_approx = 1;
	uint64_t mb_per_sec_approx = (uint64_t)mb * 1000 / ms_approx;

	printf("\n=====================================================\n");
	printf("  Workload: Throughput - %zu KB blocks\n", block_size / 1024);
	printf("=====================================================\n");
	printf("  Total read:   %u MB\n", mb);
	printf("  Total cycles: %llu\n", (unsigned long long)total_cycles);
	printf("  Cycles/byte:  %llu  (TSC-freq independent)\n",
	       (unsigned long long)cycles_per_byte);
	printf("  Throughput:   ~%llu MB/s  (assumes 3 GHz TSC)\n",
	       (unsigned long long)mb_per_sec_approx);
	printf("-----------------------------------------------------\n");
}

/* hello_ll.c is compiled with -Dmain=fuse_backend_main */
extern int fuse_backend_main(int argc, char *argv[]);

static void backend_thread_fn(void *arg)
{
	(void)arg;
	char *bk_argv[] = { "fuse", "-f", "-s", "/" };
	fuse_backend_main(4, bk_argv);
}

/* -------------------------------------------------------------------------
 * Main
 * ------------------------------------------------------------------------- */
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{
	printf("\n");
	printf("=====================================================\n");
	printf("  UKFS-FUSE Performance Benchmark (RDTSC)\n");
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
	if (!bt) {
		printf("[-] Failed to create backend thread\n");
		return 1;
	}
	uk_sched_thread_add(uk_sched_current(), bt);

	/* Wait until fuse_session_mount has registered the session */
	while (fuse_session_get_registered() == NULL)
		uk_sched_yield();

	/* Mount the FUSE filesystem */
	int ret = mount("none", "/", "fuse", 0, NULL);
	if (ret != 0) {
		printf("[-] Error mounting FUSE: %s\n", strerror(errno));
		return 1;
	}
	printf("[+] FUSE filesystem mounted at /\n");

	/* Run Benchmarks */
	bench_stat_root();
	bench_stat();
	bench_pread();
	bench_seq_read();
	bench_open_read_close();
	bench_readdir();
	
	/* Throughput benchmarks */
	bench_throughput(4 * 1024);   /* 4 KB blocks */
	bench_throughput(64 * 1024);  /* 64 KB blocks */
	bench_throughput(1024 * 1024); /* 1 MB blocks */

	printf("\n[+] Benchmarks completed.\n");
	return 0;
}
