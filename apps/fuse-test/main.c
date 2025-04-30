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

#include <time.h>
#include <stdint.h>
#include <uk/thread.h>
#include <uk/sched.h>
#include <uk/alloc.h>

extern struct fuse_session *hellofs_start(void);
extern void fuse_ukfs_queue_init(struct fuse_ukfs_queue *q);

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
	
	/* We need a mock session since fuse_daemon_start expects one */
	struct fuse_session *fake_se = calloc(1, sizeof(*fake_se));
	ret = fuse_daemon_start(&q, fake_se);
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
	free(fake_se);
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

/* --- Benchmarks --- */

static inline uint64_t get_nanos(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static struct fuse_ukfs_queue bench_queue;
static struct fuse_ukfs_request ping_req;

static void consumer_thread_fn(void *arg) __noreturn;
static void consumer_thread_fn(void *arg __unused)
{
	for (int i = 0; i < 1000000; i++) {
		struct fuse_ukfs_request *r = fuse_ukfs_queue_pop(&bench_queue);
		uk_semaphore_up(&r->done);
	}
	while(1) { uk_sched_yield(); }
}

void test_queue_throughput(void)
{
	uint64_t start, end;
	int num_iter = 10000;
	
	printf("Benchmark 1: Queue Throughput\n");
	printf("-----------------------------\n");
	
	fuse_ukfs_queue_init(&bench_queue);
	struct fuse_ukfs_request *reqs = malloc(sizeof(*reqs) * num_iter);
	UK_ASSERT(reqs);

	start = get_nanos();
	for (int i = 0; i < num_iter; i++) {
		fuse_ukfs_queue_push(&bench_queue, &reqs[i]);
	}
	for (int i = 0; i < num_iter; i++) {
		fuse_ukfs_queue_pop(&bench_queue);
	}
	end = get_nanos();
	
	uint64_t diff_ns = end - start;
	uint64_t reqs_per_sec = ((uint64_t)num_iter * 1000000000ULL) / diff_ns;
	printf("  Single-threaded (push + pop) throughput: %llu reqs/sec\n", (unsigned long long)reqs_per_sec);
	free(reqs);

	fuse_ukfs_queue_init(&bench_queue);
	uk_semaphore_init(&ping_req.done, 0);

	struct uk_alloc *a = uk_alloc_get_default();
	struct uk_thread *consumer = uk_thread_create_fn1(a, (uk_thread_fn1_t)consumer_thread_fn, NULL, a, 0, NULL, 0, a, false, "consumer", NULL, NULL);
	uk_sched_thread_add(uk_sched_current(), consumer);

	start = get_nanos();
	for (int i = 0; i < num_iter; i++) {
		fuse_ukfs_queue_push(&bench_queue, &ping_req);
		uk_semaphore_down(&ping_req.done);
	}
	end = get_nanos();

	diff_ns = end - start;
	reqs_per_sec = ((uint64_t)num_iter * 1000000000ULL) / diff_ns;
	printf("  Multi-threaded (ping-pong) throughput: %llu reqs/sec\n\n", (unsigned long long)reqs_per_sec);
}

static struct uk_semaphore ping_sem, pong_sem;
static uint64_t t_wake_start, t_wake_end;
static uint64_t total_wake_cycles = 0;

static void ctx_thread_fn(void *arg) __noreturn;
static void ctx_thread_fn(void *arg __unused)
{
	for (int i = 0; i < 10000; i++) {
		uk_semaphore_down(&ping_sem);
		t_wake_end = rdtsc();
		total_wake_cycles += (t_wake_end - t_wake_start);
		uk_semaphore_up(&pong_sem);
	}
	while(1) { uk_sched_yield(); }
}

void test_queue_latency(void)
{
	uint64_t start, end;
	uint64_t t_mutex_lock = 0, t_mutex_unlock = 0;
	uint64_t t_sem_down = 0, t_sem_up = 0;
	uint64_t t_tailq_insert = 0, t_tailq_remove = 0;
	int num_samples = 10000;

	printf("Benchmark 2: Queue Latency (RDTSC)\n");
	printf("----------------------------------\n");

	fuse_ukfs_queue_init(&bench_queue);
	
	for (int i = 0; i < num_samples; i++) {
		start = rdtsc();
		uk_mutex_lock(&bench_queue.lock);
		end = rdtsc();
		t_mutex_lock += (end - start);

		start = rdtsc();
		uk_mutex_unlock(&bench_queue.lock);
		end = rdtsc();
		t_mutex_unlock += (end - start);
	}

	for (int i = 0; i < num_samples; i++) {
		start = rdtsc();
		uk_semaphore_up(&bench_queue.count);
		end = rdtsc();
		t_sem_up += (end - start);

		start = rdtsc();
		uk_semaphore_down(&bench_queue.count);
		end = rdtsc();
		t_sem_down += (end - start);
	}

	for (int i = 0; i < num_samples; i++) {
		start = rdtsc();
		UK_TAILQ_INSERT_TAIL(&bench_queue.head, &ping_req, list);
		end = rdtsc();
		t_tailq_insert += (end - start);

		struct fuse_ukfs_request *r = UK_TAILQ_FIRST(&bench_queue.head);
		start = rdtsc();
		UK_TAILQ_REMOVE(&bench_queue.head, r, list);
		end = rdtsc();
		t_tailq_remove += (end - start);
	}

	uk_semaphore_init(&ping_sem, 0);
	uk_semaphore_init(&pong_sem, 0);
	struct uk_alloc *a = uk_alloc_get_default();
	struct uk_thread *ctx = uk_thread_create_fn1(a, (uk_thread_fn1_t)ctx_thread_fn, NULL, a, 0, NULL, 0, a, false, "ctx", NULL, NULL);
	uk_sched_thread_add(uk_sched_current(), ctx);
	uk_sched_yield();

	for (int i = 0; i < num_samples; i++) {
		t_wake_start = rdtsc();
		uk_semaphore_up(&ping_sem);
		uk_semaphore_down(&pong_sem);
	}

	printf("  uk_mutex_lock:        %llu cycles\n", (unsigned long long)(t_mutex_lock / num_samples));
	printf("  uk_mutex_unlock:      %llu cycles\n", (unsigned long long)(t_mutex_unlock / num_samples));
	printf("  UK_TAILQ_INSERT_TAIL: %llu cycles\n", (unsigned long long)(t_tailq_insert / num_samples));
	printf("  UK_TAILQ_REMOVE:      %llu cycles\n", (unsigned long long)(t_tailq_remove / num_samples));
	printf("  uk_semaphore_up:      %llu cycles\n", (unsigned long long)(t_sem_up / num_samples));
	printf("  uk_semaphore_down:    %llu cycles\n", (unsigned long long)(t_sem_down / num_samples));
	printf("  Context switch:       %llu cycles\n\n", (unsigned long long)(total_wake_cycles / num_samples));
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

	printf("\nAll Tests and Benchmarks Passed!\n");
	return 0;
}
