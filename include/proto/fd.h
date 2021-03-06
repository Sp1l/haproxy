/*
 * include/proto/fd.h
 * File descriptors states.
 *
 * Copyright (C) 2000-2014 Willy Tarreau - w@1wt.eu
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation, version 2.1
 * exclusively.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _PROTO_FD_H
#define _PROTO_FD_H

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <common/config.h>

#include <types/fd.h>

/* public variables */

extern volatile struct fdlist fd_cache;
extern volatile struct fdlist fd_cache_local[MAX_THREADS];

extern unsigned long fd_cache_mask; // Mask of threads with events in the cache

extern THREAD_LOCAL int *fd_updt;  // FD updates list
extern THREAD_LOCAL int fd_nbupdt; // number of updates in the list

__decl_hathreads(extern HA_RWLOCK_T   __attribute__((aligned(64))) fdcache_lock);    /* global lock to protect fd_cache array */

/* Deletes an FD from the fdsets.
 * The file descriptor is also closed.
 */
void fd_delete(int fd);

/* Deletes an FD from the fdsets.
 * The file descriptor is kept open.
 */
void fd_remove(int fd);

/* disable the specified poller */
void disable_poller(const char *poller_name);

/*
 * Initialize the pollers till the best one is found.
 * If none works, returns 0, otherwise 1.
 * The pollers register themselves just before main() is called.
 */
int init_pollers();

/*
 * Deinitialize the pollers.
 */
void deinit_pollers();

/*
 * Some pollers may lose their connection after a fork(). It may be necessary
 * to create initialize part of them again. Returns 0 in case of failure,
 * otherwise 1. The fork() function may be NULL if unused. In case of error,
 * the the current poller is destroyed and the caller is responsible for trying
 * another one by calling init_pollers() again.
 */
int fork_poller();

/*
 * Lists the known pollers on <out>.
 * Should be performed only before initialization.
 */
int list_pollers(FILE *out);

/*
 * Runs the polling loop
 */
void run_poller();

/* Scan and process the cached events. This should be called right after
 * the poller.
 */
void fd_process_cached_events();

void fd_add_to_fd_list(volatile struct fdlist *list, int fd);
void fd_rm_from_fd_list(volatile struct fdlist *list, int fd);

/* Mark fd <fd> as updated for polling and allocate an entry in the update list
 * for this if it was not already there. This can be done at any time.
 */
static inline void updt_fd_polling(const int fd)
{
	unsigned int oldupdt;

	/* note: we don't have a test-and-set yet in hathreads */

	if (HA_ATOMIC_BTS(&fdtab[fd].update_mask, tid))
		return;

	oldupdt = HA_ATOMIC_ADD(&fd_nbupdt, 1) - 1;
	fd_updt[oldupdt] = fd;
}

/* Allocates a cache entry for a file descriptor if it does not yet have one.
 * This can be done at any time.
 */
static inline void fd_alloc_cache_entry(const int fd)
{
	if (!(fdtab[fd].thread_mask & (fdtab[fd].thread_mask - 1)))
		fd_add_to_fd_list(&fd_cache_local[my_ffsl(fdtab[fd].thread_mask) - 1], fd);
	else
		fd_add_to_fd_list(&fd_cache, fd);
}

/* Removes entry used by fd <fd> from the FD cache and replaces it with the
 * last one.
 * If the fd has no entry assigned, return immediately.
 */
static inline void fd_release_cache_entry(const int fd)
{
	if (!(fdtab[fd].thread_mask & (fdtab[fd].thread_mask - 1)))
		fd_rm_from_fd_list(&fd_cache_local[my_ffsl(fdtab[fd].thread_mask) - 1], fd);
	else
		fd_rm_from_fd_list(&fd_cache, fd);
}

/* This function automatically enables/disables caching for an entry depending
 * on its state. It is only called on state changes.
 */
static inline void fd_update_cache(int fd)
{
	/* only READY and ACTIVE states (the two with both flags set) require a cache entry */
	if (((fdtab[fd].state & (FD_EV_READY_R | FD_EV_ACTIVE_R)) == (FD_EV_READY_R | FD_EV_ACTIVE_R)) ||
	    ((fdtab[fd].state & (FD_EV_READY_W | FD_EV_ACTIVE_W)) == (FD_EV_READY_W | FD_EV_ACTIVE_W))) {
		fd_alloc_cache_entry(fd);
	}
	else {
		fd_release_cache_entry(fd);
	}
}

/*
 * returns the FD's recv state (FD_EV_*)
 */
static inline int fd_recv_state(const int fd)
{
	return ((unsigned)fdtab[fd].state >> (4 * DIR_RD)) & FD_EV_STATUS;
}

/*
 * returns true if the FD is active for recv
 */
static inline int fd_recv_active(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_ACTIVE_R;
}

/*
 * returns true if the FD is ready for recv
 */
static inline int fd_recv_ready(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_READY_R;
}

/*
 * returns true if the FD is polled for recv
 */
static inline int fd_recv_polled(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_POLLED_R;
}

/*
 * returns the FD's send state (FD_EV_*)
 */
static inline int fd_send_state(const int fd)
{
	return ((unsigned)fdtab[fd].state >> (4 * DIR_WR)) & FD_EV_STATUS;
}

/*
 * returns true if the FD is active for send
 */
static inline int fd_send_active(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_ACTIVE_W;
}

/*
 * returns true if the FD is ready for send
 */
static inline int fd_send_ready(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_READY_W;
}

/*
 * returns true if the FD is polled for send
 */
static inline int fd_send_polled(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_POLLED_W;
}

/*
 * returns true if the FD is active for recv or send
 */
static inline int fd_active(const int fd)
{
	return (unsigned)fdtab[fd].state & FD_EV_ACTIVE_RW;
}

/* Disable processing recv events on fd <fd> */
static inline void fd_stop_recv(int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (!(old & FD_EV_ACTIVE_R))
			return;
		new = old & ~FD_EV_ACTIVE_R;
		new &= ~FD_EV_POLLED_R;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_R)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Disable processing send events on fd <fd> */
static inline void fd_stop_send(int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (!(old & FD_EV_ACTIVE_W))
			return;
		new = old & ~FD_EV_ACTIVE_W;
		new &= ~FD_EV_POLLED_W;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_W)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Disable processing of events on fd <fd> for both directions. */
static inline void fd_stop_both(int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (!(old & FD_EV_ACTIVE_RW))
			return;
		new = old & ~FD_EV_ACTIVE_RW;
		new &= ~FD_EV_POLLED_RW;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_RW)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Report that FD <fd> cannot receive anymore without polling (EAGAIN detected). */
static inline void fd_cant_recv(const int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (!(old & FD_EV_READY_R))
			return;
		new = old & ~FD_EV_READY_R;
		if (new & FD_EV_ACTIVE_R)
			new |= FD_EV_POLLED_R;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_R)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Report that FD <fd> can receive anymore without polling. */
static inline void fd_may_recv(const int fd)
{
	/* marking ready never changes polled status */
	HA_ATOMIC_OR(&fdtab[fd].state, FD_EV_READY_R);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Disable readiness when polled. This is useful to interrupt reading when it
 * is suspected that the end of data might have been reached (eg: short read).
 * This can only be done using level-triggered pollers, so if any edge-triggered
 * is ever implemented, a test will have to be added here.
 */
static inline void fd_done_recv(const int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if ((old & (FD_EV_POLLED_R|FD_EV_READY_R)) != (FD_EV_POLLED_R|FD_EV_READY_R))
			return;
		new = old & ~FD_EV_READY_R;
		if (new & FD_EV_ACTIVE_R)
			new |= FD_EV_POLLED_R;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_R)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Report that FD <fd> cannot send anymore without polling (EAGAIN detected). */
static inline void fd_cant_send(const int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (!(old & FD_EV_READY_W))
			return;
		new = old & ~FD_EV_READY_W;
		if (new & FD_EV_ACTIVE_W)
			new |= FD_EV_POLLED_W;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_W)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Report that FD <fd> can send anymore without polling (EAGAIN detected). */
static inline void fd_may_send(const int fd)
{
	/* marking ready never changes polled status */
	HA_ATOMIC_OR(&fdtab[fd].state, FD_EV_READY_W);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Prepare FD <fd> to try to receive */
static inline void fd_want_recv(int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (old & FD_EV_ACTIVE_R)
			return;
		new = old | FD_EV_ACTIVE_R;
		if (!(new & FD_EV_READY_R))
			new |= FD_EV_POLLED_R;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_R)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Prepare FD <fd> to try to send */
static inline void fd_want_send(int fd)
{
	unsigned char old, new;

	old = fdtab[fd].state;
	do {
		if (old & FD_EV_ACTIVE_W)
			return;
		new = old | FD_EV_ACTIVE_W;
		if (!(new & FD_EV_READY_W))
			new |= FD_EV_POLLED_W;
	} while (unlikely(!HA_ATOMIC_CAS(&fdtab[fd].state, &old, new)));

	if ((old ^ new) & FD_EV_POLLED_W)
		updt_fd_polling(fd);

	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fd_update_cache(fd); /* need an update entry to change the state */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* Update events seen for FD <fd> and its state if needed. This should be called
 * by the poller to set FD_POLL_* flags. */
static inline void fd_update_events(int fd, int evts)
{
	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fdtab[fd].ev &= FD_POLL_STICKY;
	fdtab[fd].ev |= evts;
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);

	if (fdtab[fd].ev & (FD_POLL_IN | FD_POLL_HUP | FD_POLL_ERR))
		fd_may_recv(fd);

	if (fdtab[fd].ev & (FD_POLL_OUT | FD_POLL_ERR))
		fd_may_send(fd);
}

/* Prepares <fd> for being polled */
static inline void fd_insert(int fd, void *owner, void (*iocb)(int fd), unsigned long thread_mask)
{
	HA_SPIN_LOCK(FD_LOCK, &fdtab[fd].lock);
	fdtab[fd].owner = owner;
	fdtab[fd].iocb = iocb;
	fdtab[fd].ev = 0;
	fdtab[fd].update_mask &= ~tid_bit;
	fdtab[fd].linger_risk = 0;
	fdtab[fd].cloned = 0;
	fdtab[fd].thread_mask = thread_mask;
	/* note: do not reset polled_mask here as it indicates which poller
	 * still knows this FD from a possible previous round.
	 */
	HA_SPIN_UNLOCK(FD_LOCK, &fdtab[fd].lock);
}

/* These are replacements for FD_SET, FD_CLR, FD_ISSET, working on uints */
static inline void hap_fd_set(int fd, unsigned int *evts)
{
	HA_ATOMIC_OR(&evts[fd / (8*sizeof(*evts))], 1U << (fd & (8*sizeof(*evts) - 1)));
}

static inline void hap_fd_clr(int fd, unsigned int *evts)
{
	HA_ATOMIC_AND(&evts[fd / (8*sizeof(*evts))], ~(1U << (fd & (8*sizeof(*evts) - 1))));
}

static inline unsigned int hap_fd_isset(int fd, unsigned int *evts)
{
	return evts[fd / (8*sizeof(*evts))] & (1U << (fd & (8*sizeof(*evts) - 1)));
}


#endif /* _PROTO_FD_H */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 * End:
 */
