// Beware, this code is shared between the kernel and userspace.

#ifdef _KERNEL
#include <types.h>
#include <lib.h>
#include <kern/errno.h>
#include <kern/secure.h>
#include <kern/test161.h>
#else
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <test161/test161.h>
#include <test161/secure.h>
#endif

// Hack for allocating userspace memory without malloc.
#define BUFFER_SIZE 4096

#ifndef _KERNEL
static char temp_buffer[BUFFER_SIZE];
static char write_buffer[BUFFER_SIZE];
#endif

static inline void * _alloc(size_t size)
{
#ifdef _KERNEL
	return kmalloc(size);
#else
	(void)size;
	return temp_buffer;
#endif
}

static inline void _free(void *ptr)
{
#ifdef _KERNEL
	kfree(ptr);
#else
	(void)ptr;
#endif
}

/*
 * Common success function for kernel tests. If SECRET_TESTING is defined,
 * ksecprintf will compute the hmac/sha256 hash of any message using the
 * shared secret and a random salt value. The (secure) server also knows
 * the secret and can verify the message was generated by a trusted source.
 * The salt value prevents against replay attacks.
 */
int
success(int status, const char * secret, const char * name) {
	if (status == TEST161_SUCCESS) {
		return secprintf(secret, "SUCCESS", name);
	} else {
		return secprintf(secret, "FAIL", name);
	}
}

#ifndef _KERNEL

// Borrowed from parallelvm.  We need atomic console writes so our
// output doesn't get intermingled since test161 works with lines.
static
int
say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(write_buffer, BUFFER_SIZE, fmt, ap);
	va_end(ap);
	return write(STDOUT_FILENO, write_buffer, strlen(write_buffer));
}
#endif

#ifndef SECRET_TESTING

int
secprintf(const char * secret, const char * msg, const char * name)
{
	(void)secret;

#ifdef _KERNEL
	return kprintf("%s: %s\n", name, msg);
#else
	return say("%s: %s\n", name, msg);
#endif
}

#else

int
secprintf(const char * secret, const char * msg, const char * name)
{
	char *hash, *salt, *fullmsg;
	int res;
	size_t len;

	hash = salt = fullmsg = NULL;

	// test161 expects "name: msg"
	len = strlen(name) + strlen(msg) + 3;	// +3 for " :" and null terminator
	fullmsg = (char *)_alloc(len);
	if (fullmsg == NULL) {
		return -ENOMEM;
	}
	snprintf(fullmsg, len, "%s: %s", name, msg);

	res = hmac_salted(fullmsg, len-1, secret, strlen(secret), &hash, &salt);
	if (res)
		return -res;

#ifdef _KERNEL
	res = kprintf("(%s, %s, %s, %s: %s)\n", name, hash, salt, name, msg);
#else
	res = say("(%s, %s, %s, %s: %s)\n", name, hash, salt, name, msg);
#endif

	_free(hash);
	_free(salt);
	_free(fullmsg);

	return res;
}

#endif
