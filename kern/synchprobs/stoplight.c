/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

/* naive implementation, 10.8-11.8s */
/* "supposedly no starvation" implementation, 11.5-12.5s, acceptable */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define LIMIT 3
/* forward definition */
struct semaphore *get_quadrant(uint32_t);

/**************** custom global variables ****************/

// quadrant semaphores
static struct semaphore *zero;
static struct semaphore *one;
static struct semaphore *two;
static struct semaphore *three;
// intersection capacity limit
static struct semaphore *limit;

// pointer to the "current" direction cycle
static struct direction_cycle *cur;

// distributed queue data structures
static struct direction_cycle *zero_dc;
static struct direction_cycle *one_dc;
static struct direction_cycle *two_dc;
static struct direction_cycle *three_dc;

/*
 * Called by the driver during initialization.
 */

void
stoplight_init()
{
    zero = sem_create("quadrant zero", 1);
    one = sem_create("quadrant one", 1);
    two = sem_create("quadrant two", 1);
    three = sem_create("quadrant three", 1);
    limit = sem_create("quadrant limiter", LIMIT);

    zero_dc = direction_cycle_create(0, "dc_cv 0", "dc_cv_lock 0");
    one_dc = direction_cycle_create(1, "dc_cv 1", "dc_cv_lock 1");
    two_dc = direction_cycle_create(2, "dc_cv 2", "dc_cv_lock 2");
    three_dc = direction_cycle_create(3, "dc_cv 3", "dc_cv_lock 3");

    zero_dc->next = one_dc;
    one_dc->next = two_dc;
    two_dc->next = three_dc;
    three_dc->next = zero_dc;

    cur = zero_dc;

    return;
}

/*
 * Called by the driver during teardown.
 */

void
stoplight_cleanup()
{
    sem_destroy(zero);
    sem_destroy(one);
    sem_destroy(two);
    sem_destroy(three);
    sem_destroy(limit);

    cur = NULL;

    direction_cycle_destroy(zero_dc);
    direction_cycle_destroy(one_dc);
    direction_cycle_destroy(two_dc);
    direction_cycle_destroy(three_dc);

    return;
}

void
turnright(uint32_t direction, uint32_t index)
{
    // (void)direction;
    // (void)index;
    /*
     * Implement this function.
     */

    struct direction_cycle *my_dc = get_direction_cycle(direction);
    struct direction_cycle *next;

    direction_cycle_wait(my_dc);
    P(limit);

    // core intersection handling code
    P(get_quadrant(direction));
    inQuadrant(direction, index);
    leaveIntersection(index);
    V(get_quadrant(direction));

    direction_cycle_signal(my_dc, &next);
    V(limit);

    return;
}

void
gostraight(uint32_t direction, uint32_t index)
{
    // (void)direction;
    // (void)index;
    /*
     * Implement this function.
     */

    struct direction_cycle *my_dc = get_direction_cycle(direction);
    struct direction_cycle *next;

    direction_cycle_wait(my_dc);
    P(limit);

    // core intersection handling code
    P(get_quadrant(direction));
    inQuadrant(direction, index);
    P(get_quadrant((direction - 1) % 4));
    inQuadrant((direction - 1) % 4, index);
    V(get_quadrant(direction));
    leaveIntersection(index);
    V(get_quadrant((direction - 1) % 4));

    direction_cycle_signal(my_dc, &next);
    V(limit);

    return;
}

void
turnleft(uint32_t direction, uint32_t index)
{
    // (void)direction;
    // (void)index;
    /*
     * Implement this function.
     */

    struct direction_cycle *my_dc = get_direction_cycle(direction);
    struct direction_cycle *next;

    direction_cycle_wait(my_dc);
    P(limit);

    // core intersection handling code
    P(get_quadrant(direction));
    inQuadrant(direction, index);
    P(get_quadrant((direction - 1) % 4));
    inQuadrant((direction - 1) % 4, index);
    V(get_quadrant(direction));
    P(get_quadrant((direction - 2) % 4));
    inQuadrant((direction - 2) % 4, index);
    V(get_quadrant((direction - 1) % 4));
    leaveIntersection(index);
    V(get_quadrant((direction - 2) % 4));

    direction_cycle_signal(my_dc, &next);
    V(limit);

    return;
}

// helper methods
struct semaphore *
get_quadrant(uint32_t num)
{
    struct semaphore *res;
    switch (num) {
    case 0:
        res = zero;
        break;
    case 1:
        res = one;
        break;
    case 2:
        res = two;
        break;
    case 3:
        res = three;
        break;
    default:
        panic("unhandled direction, should not exist");
    }

    return res;
}

/*********** direction_cycle functions  ***********/

struct direction_cycle *
direction_cycle_create(uint32_t direction, const char *cv_name,
                       const char *cv_lock_name)
{
    struct direction_cycle *to_create = kmalloc(sizeof(*to_create));
    to_create->direction = direction;
    to_create->num_cars = 0;
    to_create->cv = cv_create(cv_name);
    to_create->cv_lock = lock_create(cv_lock_name);
    to_create->next = NULL;
    return to_create;
}

void
direction_cycle_destroy(struct direction_cycle *dc)
{
    cv_destroy(dc->cv);
    lock_destroy(dc->cv_lock);
    dc->next = NULL;
    kfree(dc);
}

struct direction_cycle *
get_direction_cycle(uint32_t num)
{
    struct direction_cycle *res;
    switch (num) {
    case 0:
        res = zero_dc;
        break;
    case 1:
        res = one_dc;
        break;
    case 2:
        res = two_dc;
        break;
    case 3:
        res = three_dc;
        break;
    default:
        panic("unhandled direction, should not exist");
    }

    return res;
}

void
direction_cycle_wait(struct direction_cycle *my_dc)
{
    lock_acquire(my_dc->cv_lock);
    my_dc->num_cars++;
    while (cur->num_cars == 0) {
        cur = cur->next;
    }
    while (cur != my_dc) {
        cv_wait(my_dc->cv, my_dc->cv_lock);
        while (cur->num_cars == 0) {
            cur = cur->next;
        }
    }
    lock_release(my_dc->cv_lock);
}

void
direction_cycle_signal(struct direction_cycle *my_dc,
                       struct direction_cycle **next)
{
    lock_acquire(my_dc->cv_lock);
    my_dc->num_cars--;
    *next = my_dc->next;
    while (*next != my_dc) {
        if ((*next)->num_cars != 0)
            break;
        *next = (*next)->next;
    }
    lock_release(my_dc->cv_lock);

    lock_acquire((*next)->cv_lock);
    cv_broadcast((*next)->cv, (*next)->cv_lock);
    lock_release((*next)->cv_lock);
}