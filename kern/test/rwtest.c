/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

/* for good testing practice, all tests should only utilize the public API, and
 * not access the interior data fields, or private helper methods (though it may
 * be worthy to separately test those methods with private unit tests)
 *  Public APIs inlucde (as documented in synch.h):
 *
 *      1) struct rwlock *rwlock_create(const char *);
 *      2) void rwlock_destroy(struct rwlock *);
 *
 *      3) void rwlock_acquire_read(struct rwlock *);
 *      4) void rwlock_release_read(struct rwlock *);
 *      5) void rwlock_acquire_write(struct rwlock *);
 *      6) void rwlock_release_write(struct rwlock *);
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <current.h>
#include <kern/test161.h>
#include <spinlock.h>

#define THREADS 1000
#define INIT 0
#define READER 1
#define WRITER 2
#define DONE 3

#define TEST161_SUCCESS 0
#define TEST161_FAIL 1

/*
 * Shared initialization routines
 */

static struct lock *testlock;
static struct lock *running_lock;
static struct semaphore *donesem;
static volatile void *threads[THREADS];
static volatile struct thread *rw_threads[THREADS];
static volatile int thread_roles[THREADS];
static struct rwlock *rwlock;
struct spinlock status_lock;
static bool test_status = TEST161_FAIL;
const char *test_message;

static bool
failif(bool condition, const char *message)
{
    if (condition) {
        panic(message);
        spinlock_acquire(&status_lock);
        test_status = TEST161_FAIL;
        test_message = message;
        kprintf_n("%s", message);
        spinlock_release(&status_lock);
    }
    return condition;
}

static void
intiailize_rw_test(void)
{
    testlock = lock_create("testlock");
    running_lock = lock_create("running_lock");
    rwlock = rwlock_create("rwlock");
    donesem = sem_create("done sem", 0);

    KASSERT(testlock);
    KASSERT(running_lock);
    KASSERT(rwlock);
    KASSERT(donesem);
}

static void
cleanup_rw_test(void)
{
    lock_destroy(testlock);
    lock_destroy(running_lock);
    rwlock_destroy(rwlock);
    sem_destroy(donesem);
}

/*
 * Helper function to initialize the thread pool.
 */
static void
initialize_thread(volatile void *threads[], uint32_t index)
{
    failif((threads[index] != NULL), "failed: incorrect thread type");
    threads[index] = curthread->t_stack;
}

static void
check_rwlock_status(unsigned status)
{
    switch (status) {
    case READER:
        // if it is a reader, then no writers should be running/or ready (ready
        // may prove to be an overkill)
        for (int i = 0; i < THREADS; i++) {
            if (thread_roles[i] == WRITER) {
                failif((rw_threads[i]->t_state == S_RUN ||
                        rw_threads[i]->t_state == S_READY),
                       "Reader thread holds reader lock, while some other "
                       "writer thread could be active\n");
            }
        }
        break;
    case WRITER:
        // if it is a writer, then no other writers or readers should be
        // running/or ready (similarly, ready may prove to be an overkill)
        for (int i = 0; i < THREADS; i++) {
            if (thread_roles[i] == WRITER || thread_roles[i] == READER) {
                failif(rw_threads[i] != curthread &&
                           ((rw_threads[i]->t_state == S_RUN ||
                             rw_threads[i]->t_state == S_READY)),
                       "Writer thread holds writer lock, while some other "
                       "thread could be active\n");
            }
        }
        break;
    default:
        panic("shouldn't be here, check function argument plese\n");
    }
}

static void
reader_wrapper(void *unused1, unsigned long index)
{
    (void)unused1;

    random_yielder(4);

    lock_acquire(testlock);
    initialize_thread(threads, (uint32_t)index);
    thread_roles[index] = INIT;
    rw_threads[index] = curthread;
    lock_release(testlock);

    rwlock_acquire_read(rwlock);
    thread_roles[index] = READER;

    // ok to acquire another lock here, as it should not block anymore after
    // acquring the read lock
    lock_acquire(running_lock);
    check_rwlock_status(READER);
    lock_release(running_lock);

    random_yielder(8);

    lock_acquire(running_lock);
    check_rwlock_status(READER);
    lock_release(running_lock);

    lock_acquire(running_lock);
    // similarly, releasing the lock shouldn't block, so ok to wrap in
    // running_lock
    rwlock_release_read(rwlock);
    thread_roles[index] = DONE;
    lock_release(running_lock);

    // kprintf_n("[Reading] Reader Thread %lu ending, address is %p\n", index,
    // curthread);

    V(donesem);

    return;
}

static void
writer_wrapper(void *unused1, unsigned long index)
{
    (void)unused1;

    random_yielder(4);

    lock_acquire(testlock);
    initialize_thread(threads, (uint32_t)index);
    thread_roles[index] = DONE;
    rw_threads[index] = curthread;
    lock_release(testlock);

    rwlock_acquire_write(rwlock);
    thread_roles[index] = WRITER;

    // ok to acquire another lock here, as it should not block anymore after
    // acquring the read lock
    lock_acquire(running_lock);
    check_rwlock_status(WRITER);
    lock_release(running_lock);

    random_yielder(8);

    lock_acquire(running_lock);
    check_rwlock_status(WRITER);
    lock_release(running_lock);

    lock_acquire(running_lock);
    // similarly, releasing the lock shouldn't block, so ok to wrap in
    // running_lock
    rwlock_release_write(rwlock);
    thread_roles[index] = DONE;
    lock_release(running_lock);

    // kprintf_n("[Writing] Writer Thread %lu ending, address is %p\n", index,
    // curthread);

    V(donesem);

    return;
}

/*
 * Use these stubs to test your reader-writer locks.
 */

int
rwtest(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    int j, err = 0;
    char name[32];
    int total_count = 0;
    spinlock_init(&status_lock);
    test_status = TEST161_SUCCESS;

    intiailize_rw_test();

    /* Start readers and writers. */
    for (j = 0; j < THREADS; j++) {
        int index = j;
        threads[index] = NULL;
        int i = random() % 2;

        // to manipulate thread creations
        // i = 0;

        switch (i) {
        case 0:
            snprintf(name, sizeof(name), "Reader Thread %d", index);
            err = thread_fork(name, NULL, reader_wrapper, NULL, index);
            break;
        case 1:
            snprintf(name, sizeof(name), "Writer Thread %d", index);
            err = thread_fork(name, NULL, writer_wrapper, NULL, index);
            break;
        }
        total_count += 1;
        if (err) {
            panic("sp1: thread_fork failed: (%s)\n", strerror(err));
        }
    }

    // used as a thread join functionality, as join is not provided
    for (int k = 0; k < THREADS; k++) {
        P(donesem);
    }

    cleanup_rw_test();

    success(test_status, SECRET, "rwt1");

    return 0;
}

int
rwtest2(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt2...\n");
    kprintf_n("(This test panics on success!)\n");

    rwlock = rwlock_create("rwlock");
    if (rwlock == NULL) {
        panic("rwt2: lock_create failed\n");
    }

    secprintf(SECRET, "Should panic...", "rwt2");
    rwlock_release_read(rwlock);

    /* Should not get here on success. */

    success(TEST161_FAIL, SECRET, "rwt2");

    /* Don't do anything that could panic. */

    rwlock = NULL;
    return 0;
}

int
rwtest3(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt3...\n");
    kprintf_n("(This test panics on success!)\n");

    rwlock = rwlock_create("rwlock");
    if (rwlock == NULL) {
        panic("rwt3: rwlock_create failed\n");
    }

    secprintf(SECRET, "Should panic...", "rwt3");
    rwlock_release_write(rwlock);

    /* Should not get here on success. */

    success(TEST161_FAIL, SECRET, "rwt3");

    /* Don't do anything that could panic. */

    rwlock = NULL;
    return 0;
}

int
rwtest4(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt4...\n");
    kprintf_n("(This test panics on success!)\n");

    rwlock = rwlock_create("rwlock");
    if (rwlock == NULL) {
        panic("rwt3: lock_create failed\n");
    }

    secprintf(SECRET, "Should panic...", "rwt4");
    rwlock_acquire_read(rwlock);
    rwlock_acquire_read(rwlock);

    /* Should not get here on success. */

    success(TEST161_FAIL, SECRET, "rwt4");

    /* Don't do anything that could panic. */

    rwlock = NULL;
    return 0;
}

int
rwtest5(int nargs, char **args)
{
    (void)nargs;
    (void)args;

    kprintf_n("Starting rwt5...\n");
    kprintf_n("(This test panics on success!)\n");

    rwlock = rwlock_create("rwlock");
    if (rwlock == NULL) {
        panic("rwt3: lock_create failed\n");
    }

    secprintf(SECRET, "Should panic...", "rwt5");
    rwlock_acquire_write(rwlock);
    rwlock_acquire_write(rwlock);

    /* Should not get here on success. */

    success(TEST161_FAIL, SECRET, "rwt5");

    /* Don't do anything that could panic. */

    rwlock = NULL;
    return 0;
}
