#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#define NUM_ITERATIONS 100000

static uint64_t samples[NUM_ITERATIONS];

static inline uint64_t rdtsc(void) {
	uint32_t lo, hi;
	__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
}

static int cmp_u64(const void *a, const void *b) {
	uint64_t arg1 = *(const uint64_t *)a;
	uint64_t arg2 = *(const uint64_t *)b;
	if (arg1 < arg2) return -1;
	if (arg1 > arg2) return 1;
	return 0;
}

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

static void bench_stat(const char *path) {
	struct stat st;
	for (int i = 0; i < 100; i++) stat(path, &st);
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		stat(path, &st);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("stat() - LOOKUP+GETATTR", samples, NUM_ITERATIONS);
}

static void bench_stat_root(const char *dir_path) {
	struct stat st;
	for (int i = 0; i < 100; i++) stat(dir_path, &st);
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		stat(dir_path, &st);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("stat(mountpoint) - GETATTR only (1 round-trip)", samples, NUM_ITERATIONS);
}

static void bench_seq_read(const char *path) {
	char buf[16];
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	for (int i = 0; i < 100; i++) {
		lseek(fd, 0, SEEK_SET);
		read(fd, buf, 5);
		read(fd, buf, 8);
	}
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		lseek(fd, 0, SEEK_SET);
		uint64_t t0 = rdtsc();
		read(fd, buf, 5);
		read(fd, buf, 8);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	close(fd);
	print_stats("seq read() 5+8 bytes - 2 FUSE READs", samples, NUM_ITERATIONS);
}

static void bench_open_read_close(const char *path) {
	char buf[16];
	const int count = 1000;
	for (int i = 0; i < count; i++) {
		uint64_t t0 = rdtsc();
		int fd = open(path, O_RDONLY);
		if (fd >= 0) {
			pread(fd, buf, 13, 0);
			close(fd);
		}
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("open+pread+close - incl. FUSE OPEN", samples, count);
}

static void bench_pread(const char *path) {
	char buf[16];
	int fd = open(path, O_RDONLY);
	if (fd < 0) return;
	for (int i = 0; i < 100; i++) pread(fd, buf, 13, 0);
	for (int i = 0; i < NUM_ITERATIONS; i++) {
		uint64_t t0 = rdtsc();
		pread(fd, buf, 13, 0);
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	close(fd);
	print_stats("pread() - Data Transfer", samples, NUM_ITERATIONS);
}

static void bench_readdir(const char *dir_path) {
	int count = NUM_ITERATIONS / 10;

	/* Warmup */
	for (int i = 0; i < 20; i++) {
		DIR *dp = opendir(dir_path);
		if (dp) { while (readdir(dp)) {} closedir(dp); }
	}

	for (int i = 0; i < count; i++) {
		uint64_t t0 = rdtsc();
		DIR *dp = opendir(dir_path);
		if (dp) {
			while (readdir(dp)) { }
			closedir(dp);
		}
		uint64_t t1 = rdtsc();
		samples[i] = t1 - t0;
	}
	print_stats("opendir + readdir + closedir", samples, count);
}

static void bench_throughput(const char *path, size_t block_size) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("Failed to open %s for throughput bench\n", path);
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
	/* Assuming ~3.0 GHz CPU for rough MB/s estimation in cycles */
	uint64_t milliseconds = total_cycles / 3000000;
	uint32_t mb = bytes_read / (1024 * 1024);
	
	if (milliseconds == 0) milliseconds = 1;
	uint32_t mb_per_sec = (mb * 1000) / milliseconds;

	printf("\n=====================================================\n");
	printf("  Workload: Throughput (/bigfile) - %zu KB blocks\n", block_size / 1024);
	printf("=====================================================\n");
	printf("  Total read: %u MB\n", mb);
	printf("  Time:       %llu cycles (~%u ms)\n", (unsigned long long)total_cycles, (unsigned)milliseconds);
	printf("  Throughput: %u MB/s\n", mb_per_sec);
	printf("-----------------------------------------------------\n");
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <mountpoint>\n", argv[0]);
		return 1;
	}
	char file_path[256];
	snprintf(file_path, sizeof(file_path), "%s/hello", argv[1]);

	printf("\n[+] Linux FUSE Native Benchmark on %s\n", argv[1]);
	bench_stat_root(argv[1]);
	bench_stat(file_path);
	bench_pread(file_path);
	bench_seq_read(file_path);
	bench_open_read_close(file_path);
	bench_readdir(argv[1]);
	
	char bigfile_path[256];
	snprintf(bigfile_path, sizeof(bigfile_path), "%s/bigfile", argv[1]);
	bench_throughput(bigfile_path, 4 * 1024);
	bench_throughput(bigfile_path, 64 * 1024);
	bench_throughput(bigfile_path, 1024 * 1024);
	
	return 0;
}
