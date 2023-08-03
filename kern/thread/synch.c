/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
    struct semaphore *sem;

    sem = kmalloc(sizeof(*sem));
    if (sem == NULL) {
        return NULL;
    }

    sem->sem_name = kstrdup(name);
    if (sem->sem_name == NULL) {
        kfree(sem);
        return NULL;
    }

    sem->sem_wchan = wchan_create(sem->sem_name);
    if (sem->sem_wchan == NULL) {
        kfree(sem->sem_name);
        kfree(sem);
        return NULL;
    }

    spinlock_init(&sem->sem_lock);
    sem->sem_count = initial_count;

    return sem;
}

void
sem_destroy(struct semaphore *sem)
{
    KASSERT(sem != NULL);

    /* wchan_cleanup will assert if anyone's waiting on it */
    spinlock_cleanup(&sem->sem_lock);
    wchan_destroy(sem->sem_wchan);
    kfree(sem->sem_name);
    kfree(sem);
}

void
P(struct semaphore *sem)
{
    KASSERT(sem != NULL);

    /*
     * May not block in an interrupt handler.
     *
     * For robustness, always check, even if we can actually
     * complete the P without blocking.
     */
    KASSERT(curthread->t_in_interrupt == false);

    /* Use the semaphore spinlock to protect the wchan as well. */
    spinlock_acquire(&sem->sem_lock);
    while (sem->sem_count == 0) {
        /*
         *
         * Note that we don't maintain strict FIFO ordering of
         * threads going through the semaphore; that is, we
         * might "get" it on the first try even if other
         * threads are waiting. Apparently according to some
         * textbooks semaphores must for some reason have
         * strict ordering. Too bad. :-)
         *
         * Exercise: how would you implement strict FIFO
         * ordering?
         */
        wchan_sleep(sem->sem_wchan, &sem->sem_lock);
    }
    KASSERT(sem->sem_count > 0);
    sem->sem_count--;
    spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
    KASSERT(sem != NULL);

    spinlock_acquire(&sem->sem_lock);

    sem->sem_count++;
    KASSERT(sem->sem_count > 0);
    wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

    spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
    struct lock *lock;

    lock = kmalloc(sizeof(*lock));
    if (lock == NULL) {
        return NULL;
    }

    lock->lk_name = kstrdup(name);
    if (lock->lk_name == NULL) {
        kfree(lock);
        return NULL;
    }

    HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

    // add stuff here as needed

    // initially lock shouldn't have holder
    lock->lk_holder = NULL;

    // initializing wake channels
    lock->lk_wchan = wchan_create(lock->lk_name);
    if (lock->lk_wchan == NULL) {
        kfree(lock->lk_name);
        kfree(lock);
        return NULL;
    }

    // initializing spinlock
    spinlock_init(&lock->lk_lock);

    return lock;
}

void
lock_destroy(struct lock *lock)
{
    KASSERT(lock != NULL);

    // add stuff here as needed

    KASSERT(!lock->lk_holder);

    spinlock_cleanup(&lock->lk_lock);
    wchan_destroy(lock->lk_wchan);
    kfree(lock->lk_name);
    kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
    KASSERT(lock != NULL);
    KASSERT(!lock_do_i_hold(lock));

    /*
     * May not block in an interrupt handler.
     *
     * For robustness, always check, even if we can actually
     * complete the acquire without blocking.
     */
    KASSERT(curthread->t_in_interrupt == false);

    /* Use the lock spinlock to protect the wchan as well. */
    spinlock_acquire(&lock->lk_lock);

    /* Call this (atomically) before waiting for a lock */
    HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

    while (lock->lk_holder) {

        wchan_sleep(lock->lk_wchan, &lock->lk_lock);
    }
    lock->lk_holder = curthread;
    KASSERT(lock->lk_holder);

    /* Call this (atomically) once the lock is acquired */
    HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);

    // (void)lock; // suppress warning until code gets written

    spinlock_release(&lock->lk_lock);
}

void
lock_release(struct lock *lock)
{
    KASSERT(lock != NULL);

    // only the thread holding the lock may do this
    KASSERT(lock_do_i_hold(lock));

    // Write this

    spinlock_acquire(&lock->lk_lock);

    lock->lk_holder = NULL;
    KASSERT(!lock->lk_holder);

    wchan_wakeone(lock->lk_wchan, &lock->lk_lock);

    /* Call this (atomically) when the lock is released */
    HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

    spinlock_release(&lock->lk_lock);

    // (void)lock; // suppress warning until code gets written
}

bool
lock_do_i_hold(struct lock *lock)
{
    // Write this

    // KASSERT(lock);

    // (void)lock;  // suppress warning until code gets written

    // return true; // dummy until code gets written

    // assumptions to have this code run atomically enough
    return (lock->lk_holder == curthread);
}

////////////////////////////////////////////////////////////
//
// CV.

struct cv *
cv_create(const char *name)
{
    struct cv *cv;

    cv = kmalloc(sizeof(*cv));
    if (cv == NULL) {
        return NULL;
    }

    cv->cv_name = kstrdup(name);
    if (cv->cv_name == NULL) {
        kfree(cv);
        return NULL;
    }

    // add stuff here as needed

    // initializing wake channels
    cv->cv_wchan = wchan_create(cv->cv_name);
    if (cv->cv_wchan == NULL) {
        kfree(cv->cv_name);
        kfree(cv);
        return NULL;
    }

    // initializing spinlock
    spinlock_init(&cv->cv_lock);

    return cv;
}

void
cv_destroy(struct cv *cv)
{
    KASSERT(cv != NULL);

    // add stuff here as needed

    spinlock_cleanup(&cv->cv_lock);
    wchan_destroy(cv->cv_wchan);
    kfree(cv->cv_name);
    kfree(cv);
}

void
loose_cv_wait(struct cv *cv, struct lock *lock)
{
    // Write this
    // (void)cv;   // suppress warning until code gets written
    // (void)lock; // suppress warning until code gets written

    KASSERT(cv);
    KASSERT(lock_do_i_hold(lock));

    // it is fine to release the lock then go to sleep, because we aren't doing
    // anything useful anyway
    spinlock_acquire(&cv->cv_lock);
    lock_release(lock);
    loose_wchan_sleep(cv->cv_wchan, &cv->cv_lock);

    // run concurrently, go wild! as the lock would be destroyed upon usage
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
    // Write this
    // (void)cv;   // suppress warning until code gets written
    // (void)lock; // suppress warning until code gets written

    KASSERT(cv);
    KASSERT(lock_do_i_hold(lock));

    // it is fine to release the lock then go to sleep, because we aren't doing
    // anything useful anyway
    spinlock_acquire(&cv->cv_lock);
    lock_release(lock);
    wchan_sleep(cv->cv_wchan, &cv->cv_lock);

    // the order here seems important, need further understanding/investigation
    // TODO..
    spinlock_release(&cv->cv_lock);
    lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
    // Write this
    // (void)cv;   // suppress warning until code gets written
    // (void)lock; // suppress warning until code gets written

    KASSERT(cv);
    KASSERT(lock_do_i_hold(lock));

    spinlock_acquire(&cv->cv_lock);
    wchan_wakeone(cv->cv_wchan, &cv->cv_lock);
    spinlock_release(&cv->cv_lock);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
    // Write this
    // (void)cv;   // suppress warning until code gets written
    // (void)lock; // suppress warning until code gets written

    KASSERT(cv);
    KASSERT(lock_do_i_hold(lock));

    spinlock_acquire(&cv->cv_lock);
    wchan_wakeall(cv->cv_wchan, &cv->cv_lock);
    spinlock_release(&cv->cv_lock);
}

////////////////////////////////////////////////////////////
//
// Reader-Writeer Lock.

struct rwlock *
rwlock_create(const char *name)
{
    struct rwlock *rwlock;

    rwlock = kmalloc(sizeof(*rwlock));
    if (rwlock == NULL) {
        return NULL;
    }

    rwlock->rwlock_name = kstrdup(name);
    if (rwlock->rwlock_name == NULL) {
        kfree(rwlock);
        return NULL;
    }

    // intialize the lock that protects the counts for rwlock
    rwlock->lock = lock_create(name);
    if (rwlock->lock == NULL) {
        kfree(rwlock->rwlock_name);
        kfree(rwlock);
        return NULL;
    }

    // all other fields should have initial values (0 or NULL, FREE)
    rwlock->status = FREE;
    rwlock->total_num_writers = 0;
    rwlock->active_readers = NULL;
    rwlock->active_writer = NULL;
    rwlock->req_head = NULL;
    rwlock->req_tail = NULL;

    rwlock->naming_counter = 0;

    return rwlock;
}

void
rwlock_destroy(struct rwlock *rwlock)
{
    KASSERT(rwlock);
    KASSERT(rwlock->total_num_writers == 0);
    KASSERT(rwlock->status == FREE);
    KASSERT(!rwlock->req_head);
    KASSERT(!rwlock->req_tail);

    lock_destroy(rwlock->lock);
    kfree(rwlock->rwlock_name);
    kfree(rwlock);
}

void
rwlock_acquire_read(struct rwlock *rwlock)
{
    KASSERT(rwlock);

    lock_acquire(rwlock->lock);

    KASSERT(
        !rwlock_do_i_hold_reader(rwlock)); // should not acquire multiple times

    switch (rwlock->status) {
    case FREE:
        // easy, just put it into the active readers
        KASSERT(!rwlock->active_readers);
        KASSERT(!rwlock->active_writer);
        rwlock->status = READ;
        rwlock->active_readers = reader_q_create();
        reader_q_insert(rwlock->active_readers, curthread);
        break;

    case READ:
        /* if no writers waiting, MUST BE READ, and no reqeust waiting */
        if (rwlock->total_num_writers == 0) {
            KASSERT(rwlock->active_readers);
            KASSERT(!rwlock->req_head);
            KASSERT(!rwlock->req_tail);
            KASSERT(!rwlock->active_writer);

            // simply insert the reader into the active readers
            reader_q_insert(rwlock->active_readers, curthread);
            break;
        }

    case WRITE:
        // the handling is surprisingly the same

        // Sanity Check
        KASSERT(
            (!(rwlock->total_num_writers == 0) && (rwlock->status == WRITE)));

        /* there's some writer **waiting**, or the first one is a writer */

        if (rwlock->req_tail->type == READ) {
            /* last request is read, just add on to the list, then wait on cv */
            lock_acquire(rwlock->req_tail->req_cv_lock);
            reader_q_insert(rwlock->req_tail->readers, curthread);
            lock_release(rwlock->lock);
            loose_cv_wait(rwlock->req_tail->req_cv,
                          rwlock->req_tail->req_cv_lock);
            return;
        } else if (rwlock->req_tail->type == WRITE) {
            /* last req is write, need to add a new READ req */
            request_q_insert(rwlock, READ, curthread);
            return;
        }

        // honestly shouldn't be here
        panic("why am I here? [1]\n");
    }

    lock_release(rwlock->lock);
}

void
rwlock_acquire_write(struct rwlock *rwlock)
{
    KASSERT(rwlock);

    lock_acquire(rwlock->lock);

    KASSERT(
        !rwlock_do_i_hold_writer(rwlock)); // should not acquire multiple times

    switch (rwlock->status) {
    case FREE:
        // easy, just put it into the active readers
        KASSERT(!rwlock->active_readers);
        KASSERT(!rwlock->active_writer);
        rwlock->status = WRITE;
        rwlock->active_writer = curthread;
        rwlock->total_num_writers++;
        break;

    case READ:
    case WRITE:
        // really doesn't matter, as needs to create a new request anyway
        request_q_insert(rwlock, WRITE, curthread);
        return;
    }

    lock_release(rwlock->lock);
}

void
rwlock_release_read(struct rwlock *rwlock)
{
    KASSERT(rwlock);
    lock_acquire(rwlock->lock);

    KASSERT(rwlock_do_i_hold_reader(rwlock));
    KASSERT(rwlock->status == READ);

    reader_q_remove(rwlock->active_readers, curthread);

    /* CASE: if not on to next request, then don't need to do more */
    if (rwlock->active_readers->size != 0) {
        lock_release(rwlock->lock);
        return;
    }

    // otherwise this request should be removed, and the next request should be
    // processed

    /* CASE: no pending requests */
    if (rwlock->req_head == NULL || rwlock->req_tail == NULL ||
        rwlock->total_num_writers == 0) {
        KASSERT(!rwlock->req_head);
        KASSERT(!rwlock->req_tail);
        KASSERT(rwlock->total_num_writers == 0);

        // update the current state to be free
        rwlock->status = FREE;

        lock_release(rwlock->lock);
        return;
    }

    // TODO: DUPLICATE CODE
    /* CASE: has pending requests */
    struct request *pending = rwlock->req_head;

    /*      one last pending request */
    if (rwlock->req_head == rwlock->req_tail)
        rwlock->req_tail = NULL;

    rwlock->req_head = rwlock->req_head->next;
    rwlock->status = pending->type;
    pending->next = NULL;

    // now we deal with the pending request
    switch (pending->type) {
    case READ:
        rwlock->active_readers = pending->readers;
        break;
    case WRITE:
        rwlock->active_writer = pending->writer;
        break;
    default:
        panic("why am I here? [2]\n");
    }

    // now wake up all the waiting requests, and free the cv afterwards
    lock_acquire(pending->req_cv_lock);
    cv_broadcast(pending->req_cv, pending->req_cv_lock);
    lock_release(pending->req_cv_lock);

    // now can safely destory the cv and the lock?
    cv_destroy(pending->req_cv);
    lock_destroy(pending->req_cv_lock);

    // now go wild!
    lock_release(rwlock->lock);
}

void
rwlock_release_write(struct rwlock *rwlock)
{
    KASSERT(rwlock);
    lock_acquire(rwlock->lock);

    KASSERT(rwlock_do_i_hold_writer(rwlock));
    KASSERT(rwlock->status == WRITE);

    rwlock->total_num_writers--;
    rwlock->active_writer = NULL;

    /* no more pending requests, just free now */
    if (rwlock->req_head == NULL || rwlock->req_tail == NULL) {
        KASSERT(!rwlock->req_head);
        KASSERT(!rwlock->req_tail);

        KASSERT(rwlock->total_num_writers == 0);

        rwlock->status = FREE;
        lock_release(rwlock->lock);
        return;
    }

    /* doesn't matter what the next request type is, all want to
     *  1) move the pending requests' readers/writer to active
     *  2) clean up the remains
     */

    // TODO: DUPLICATE CODE
    struct request *pending = rwlock->req_head;

    /* one last pending request */
    if (rwlock->req_head == rwlock->req_tail)
        rwlock->req_tail = NULL;

    rwlock->req_head = rwlock->req_head->next;
    rwlock->status = pending->type;
    pending->next = NULL;

    // now we deal with the pending request
    switch (pending->type) {
    case READ:
        rwlock->active_readers = pending->readers;
        break;
    case WRITE:
        rwlock->active_writer = pending->writer;
        break;
    default:
        panic("why am I here? [3]\n");
    }

    // now wake up all the waiting requests, and free the cv afterwards
    lock_acquire(pending->req_cv_lock);
    cv_broadcast(pending->req_cv, pending->req_cv_lock);
    lock_release(pending->req_cv_lock);

    // now can safely destory the cv and the lock?
    cv_destroy(pending->req_cv);
    lock_destroy(pending->req_cv_lock);

    // now go wild!
    lock_release(rwlock->lock);
}

/********** helper methods for reader-writer lock **********/

struct reader_q *
reader_q_create()
{
    struct reader_q *rq = kmalloc(sizeof(*rq));
    KASSERT(rq);
    rq->head = NULL;
    rq->tail = NULL;
    rq->size = 0;
    return rq;
}

void
reader_q_destroy(struct reader_q *rq)
{
    KASSERT(!rq->head);
    KASSERT(!rq->tail);
    KASSERT(rq->size == 0);
    kfree(rq);
}

void
reader_q_insert(struct reader_q *rq, struct thread *t_to_insert)
{
    KASSERT(rq);
    KASSERT(t_to_insert);

    // creating the link-list element
    struct thread_ll *to_insert = kmalloc(sizeof(*to_insert));
    KASSERT(to_insert);
    to_insert->t = t_to_insert;
    to_insert->next = NULL;

    // case rq is empty
    if (rq->size == 0 || rq->head == NULL || rq->tail == NULL) {
        KASSERT(!rq->head);
        KASSERT(!rq->tail);
        KASSERT(rq->size == 0);

        rq->head = to_insert;
        rq->tail = to_insert;
        (rq->size)++;

        return;
    }

    // case rq only has one element
    if (rq->size == 1 || rq->head == rq->tail) {
        KASSERT(rq->head);
        KASSERT(rq->tail);
        KASSERT(rq->head == rq->tail);
        KASSERT(rq->size == 1);

        rq->head->next = to_insert;
        rq->tail = to_insert;
        (rq->size)++;

        return;
    }

    // normal case
    rq->tail->next = to_insert;
    rq->tail = rq->tail->next;
    (rq->size)++;

    return;
}

void
reader_q_remove(struct reader_q *rq, struct thread *t_to_remove)
{
    KASSERT(rq);
    KASSERT(rq->head);
    KASSERT(rq->tail);
    KASSERT(rq->size > 0);

    struct thread_ll *cur = rq->head;
    struct thread_ll *prev = NULL;
    while (cur) {
        // found
        if (cur->t == t_to_remove) {
            if (prev == NULL) {
                // cur is the head
                KASSERT(cur == rq->head);
                rq->head = rq->head->next;
                (rq->size)--;

                // have to reset tail as well, if now rq is empty
                if (rq->size == 0)
                    rq->tail = NULL;

                // free the cur/remove
                cur->next = NULL;
                kfree(cur);
                return;
            }

            prev->next = cur->next;
            (rq->size)--;

            cur->next = NULL;
            kfree(cur);
            return;
        }

        // proceed to the next element
        prev = cur;
        cur = cur->next;
    }

    // should not get here, which would mean we are removing something that
    // isn't in the list at the first place
    panic("reader_q_remove failed!\n");
}

unsigned int
digits(unsigned int num)
{
    unsigned int counter = 0;
    while (num) {
        num /= 10;
        counter++;
    }
    return counter;
}

void
get_req_names(unsigned int num, char **cv_name, char **lock_name)
{
    unsigned int length = digits(num);
    *cv_name = kmalloc(length + 8 + 3 + 1);
    KASSERT(cv_name);
    *lock_name = kmalloc(length + 8 + 5 + 1);
    KASSERT(lock_name);

    strcpy(*cv_name, "request_cv ");
    strcpy(*lock_name, "request_lock ");
    for (unsigned int i = 0; i < length; i++) {
        (*cv_name)[length + 8 + 3 - i - 1] = num % 10 + '0';
        (*lock_name)[length + 8 + 5 - i - 1] = num % 10 + '0';
        num /= 10;
    }
    (*cv_name)[length + 8 + 3] = '\0';
    (*lock_name)[length + 8 + 5] = '\0';
}

bool
rwlock_do_i_hold_writer(struct rwlock *rwlock)
{
    KASSERT(rwlock);
    KASSERT(lock_do_i_hold(rwlock->lock));
    KASSERT(rwlock->lock->lk_holder == curthread);

    return (curthread == rwlock->active_writer);
}

bool
rwlock_do_i_hold_reader(struct rwlock *rwlock)
{
    KASSERT(rwlock);
    KASSERT(lock_do_i_hold(rwlock->lock));
    KASSERT(rwlock->lock->lk_holder == curthread);

    struct reader_q *rq;
    struct thread_ll *cur;

    // if both values are non-null
    if ((rq = rwlock->active_readers) && (cur = rq->head)) {
        while (cur) {
            if (cur->t == curthread)
                return true;
            cur = cur->next;
        }
    }

    return false;
}

/* CARE: Releases the rwlock and waits on cv, should return immediately after */
void
request_q_insert(struct rwlock *rwlock, status_t status, struct thread *t)
{
    KASSERT(rwlock);
    KASSERT(lock_do_i_hold(rwlock->lock));

    // request creation
    struct request *new_req = kmalloc(sizeof(*new_req));
    KASSERT(new_req);
    new_req->type = status;
    new_req->next = NULL;
    new_req->readers = NULL;
    new_req->writer = NULL;

    char *req_cv_name, *req_cv_lock_name;
    get_req_names(++(rwlock->naming_counter), &req_cv_name, &req_cv_lock_name);
    new_req->req_cv = cv_create(req_cv_name);
    KASSERT(new_req->req_cv);
    new_req->req_cv_lock = lock_create(req_cv_lock_name);
    KASSERT(new_req->req_cv_lock);
    kfree(req_cv_name);
    kfree(req_cv_lock_name);

    lock_acquire(new_req->req_cv_lock);
    switch (status) {
    case READ:
        new_req->readers = reader_q_create();
        reader_q_insert(new_req->readers, t);
        break;

    case WRITE:
        new_req->writer = t;
        rwlock->total_num_writers++;
        break;

    default:
        panic("why am I here? [4]\n");
    }

    // holding on to the request lock, actually add the request to the request
    // list, then wait on the cv

    /* case for empty request list */
    if (rwlock->req_head == NULL || rwlock->req_tail == NULL) {
        KASSERT(!rwlock->req_head);
        KASSERT(!rwlock->req_tail);

        rwlock->req_head = new_req;
        rwlock->req_tail = new_req;
    }

    /* case for only one element in the request list */
    else if (rwlock->req_head == rwlock->req_tail) {
        rwlock->req_head->next = new_req;
        rwlock->req_tail = new_req;
    }

    /* normal case, in the middle */
    else {
        rwlock->req_tail->next = new_req;
        rwlock->req_tail = new_req;
    }

    // wait on the cv
    lock_release(rwlock->lock);
    loose_cv_wait(new_req->req_cv, new_req->req_cv_lock);
}