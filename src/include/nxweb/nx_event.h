/*
 * Copyright (c) 2011-2012 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXWEB.
 *
 * NXWEB is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXWEB is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXWEB. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NX_EVENT_H
#define	NX_EVENT_H

#ifdef	__cplusplus
extern "C" {
#endif

#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <sys/epoll.h>
//#include <sys/eventfd.h>

#include "nx_pool.h"

#define NXE_NUMBER_OF_TIMER_QUEUES 8
#define NXE_FREE_EVENT_POOL_INITIAL_SIZE 16

/*
 * Software components implement interfaces (istream, ostream, publisher, subscriber, timer).
 * Events are generated (emitted) by event sources
 * (fd_source, eventfd_source, stream_connection, timer, publisher)
 * and delivered to interfaces.
 * Events are:
 *  - timer event
 *  - message event (emitted by publishers)
 *  - ready to communicate event (triggered when both ends of stream connection become ready)
 * Events are generated either "physically" (from epoll) or "virtually" (by calling function).
 *
 * Event object is created when event is emitted and is destroyed after it is delivered.
 * Event object could be embedded inside event source and reused (if there could only be
 * single instance of event from the same source: all except publisher's messages).
 * Event source itself could be embedded inside interface.
 */

typedef _Bool nxe_bits_t;
typedef uint32_t nxe_flags_t;
typedef uint64_t nxe_time_t;
typedef size_t nxe_size_t;
typedef ssize_t nxe_ssize_t;

struct nxe_interface_base;
struct nxe_istream;
struct nxe_ostream;
struct nxe_publisher;
struct nxe_subscriber;
struct nxe_timer;
struct nxe_event;
struct nxe_loop;

enum nxe_error_code {
  NXE_ERROR=-1,
  NXE_RDHUP=-2,
  NXE_HUP=-3,
  NXE_STALE=-4,
  NXE_RDCLOSED=-5,
  NXE_WRITTEN_NONE=-6,
  NXE_PROTO_ERROR=-7
};

enum nxe_flags {
  NXEF_EOF=0x1
};

typedef union nxe_data {
  int i;
  long l;
  int32_t i32;
  int64_t i64;
  uint32_t u32;
  uint64_t u64;
  nxe_size_t sz;
  nxe_ssize_t ssz;
  off_t offs;
  void* ptr;
  const void* cptr;
  char* ptrc;
  const char* cptrc;
} nxe_data;

typedef union nxe_interface {
  struct nxe_interface_base* itf;
  struct nxe_istream* is;
  struct nxe_ostream* os;
  struct nxe_publisher* pub;
  struct nxe_subscriber* sub;
  struct nxe_timer* timer;
} nxe_interface;

typedef union nxe_sender {
  struct nxe_interface_base* itf;
  struct nxe_istream* is;
  struct nxe_publisher* pub;
} nxe_sender;

typedef union nxe_receiver {
  struct nxe_interface_base* itf;
  struct nxe_ostream* os;
  struct nxe_subscriber* sub;
  struct nxe_timer* timer;
  void (*callback)(nxe_data data);
} nxe_receiver;

typedef struct nxe_event_class {
  void (*deliver)(struct nxe_event* evt);
  void (*free)(struct nxe_loop* loop, struct nxe_event* evt);
  //void (*on_stale)(struct nxe_event* evt);
  //nxe_bits_t log_stale:1;
} nxe_event_class;

extern const nxe_event_class* const NXE_EV_STREAM;
extern const nxe_event_class* const NXE_EV_MESSAGE;
//extern const nxe_event_class* const NXE_EV_TIMER;
extern const nxe_event_class* const NXE_EV_CALLBACK;

typedef struct nxe_event {
  const nxe_event_class* cls;
  struct nxe_loop* loop;
  nxe_receiver receiver;
  nxe_data data;
  struct nxe_event* next_by_receiver; // used for subscriber active events lists
  struct nxe_event* prev;
  struct nxe_event* next;
} nxe_event;

typedef struct nxe_interface_base_class {
  //nxe_bits_t log_stale:1;
  //void (*on_detach)(struct nxe_interface_base* itf);
} nxe_interface_base_class;

typedef union {
  const struct nxe_istream_class* is_cls;
  const struct nxe_ostream_class* os_cls;
  const struct nxe_publisher_class* pub_cls;
  const struct nxe_subscriber_class* sub_cls;
  const struct nxe_timer_class* timer_cls;
} nxe_interface_class;

typedef struct nxe_interface_base {
  nxe_interface_class cls;
  struct nxe_loop* loop;
} nxe_interface_base;

typedef struct nxe_istream_class {
  nxe_interface_base_class super;
  void (*do_write)(struct nxe_istream* is, struct nxe_ostream* os);
  nxe_size_t (*read)(struct nxe_istream* is, struct nxe_ostream* os, void* ptr, nxe_size_t size, nxe_flags_t* flags);
} nxe_istream_class;

typedef struct nxe_istream {
  nxe_interface_base super;
  nxe_bits_t ready:1;
  nxe_bits_t part_of_fd_source:1;
  struct nxe_ostream* pair;
  struct nxe_event evt; // embedded event
} nxe_istream;

struct nx_file_reader;

typedef struct nxe_ostream_class {
  nxe_interface_base_class super;
  void (*do_read)(struct nxe_ostream* os, struct nxe_istream* is);
  nxe_ssize_t (*write)(struct nxe_ostream* os, struct nxe_istream* is, int fd, struct nx_file_reader* fr, nxe_data ptr, nxe_size_t size, nxe_flags_t* flags); // fd & fr are 0 for memory ptr
  void (*shutdown)(struct nxe_ostream* os);
} nxe_ostream_class;

typedef struct nxe_ostream {
  nxe_interface_base super;
  nxe_bits_t ready:1;
  nxe_bits_t part_of_fd_source:1;
  //nxe_bits_t batch_write_started:1;
  struct nxe_istream* pair;
} nxe_ostream;

typedef struct nxe_publisher_class {
  nxe_interface_base_class super;
  void (*do_publish)(struct nxe_publisher* pub, struct nxe_subscriber* sub, nxe_data data);
} nxe_publisher_class;

extern const nxe_publisher_class* const NXE_PUB_DEFAULT;

typedef struct nxe_publisher {
  nxe_interface_base super;
  nxe_bits_t part_of_fd_source:1;
  nxe_bits_t part_of_efd_source:1;
  nxe_bits_t part_of_lfd_source:1;
  struct nxe_subscriber* sub;
} nxe_publisher;

typedef struct nxe_subscriber_class {
  nxe_interface_base_class super;
  void (*on_message)(struct nxe_subscriber* sub, struct nxe_publisher* pub, nxe_data data);
} nxe_subscriber_class;

typedef struct nxe_subscriber {
  nxe_interface_base super;
  struct nxe_publisher* pub;
  struct nxe_subscriber* next; // for multi-cast
  struct nxe_event* evt; // list of active events to this subscriber
} nxe_subscriber;

typedef struct nxe_timer_class {
  nxe_interface_base_class super;
  void (*on_timeout)(struct nxe_timer* timer, nxe_data data);
} nxe_timer_class;

typedef struct nxe_timer {
  nxe_interface_base super;
  nxe_time_t abs_time;
  nxe_data data;
  struct nxe_timer* next;
  struct nxe_timer* prev;
  //struct nxe_event evt; // embedded event
} nxe_timer;

typedef union nxe_event_source {
  struct nxe_fd_source* fs;
  struct nxe_eventfd_source* efs;
  struct nxe_listenfd_source* lfs;
} nxe_event_source;

typedef struct nxe_event_source_class {
  void (*emit)(struct nxe_loop* loop, nxe_event_source source, uint32_t epoll_events);
} nxe_event_source_class;

extern const nxe_event_source_class* const NXE_ES_FD;
extern const nxe_event_source_class* const NXE_ES_EFD;
extern const nxe_event_source_class* const NXE_ES_LFD;

typedef struct nxe_fd_source {
  const nxe_event_source_class* cls;
  int fd;
  nxe_istream data_is;
  nxe_ostream data_os;
  nxe_publisher data_error;
} nxe_fd_source;

typedef struct nxe_eventfd_source {
  const nxe_event_source_class* cls;
  int fd[2]; // for eventfd/pipe async notifications
  nxe_publisher data_notify;
} nxe_eventfd_source;

typedef struct nxe_listenfd_source {
  const nxe_event_source_class* cls;
  int fd;
  nxe_publisher data_notify;
} nxe_listenfd_source;

typedef struct nxe_timer_queue {
  nxe_time_t timeout;
  nxe_timer* timer_first;
  nxe_timer* timer_last;
} nxe_timer_queue;

typedef struct nxe_loop {
  nxe_time_t current_time;
  nxe_time_t last_http_time;
  char http_time_str[32];
  char iso8601_time_str[24];
  time_t http_time;

  volatile _Bool broken;
  int ref_count;

  int epoll_fd;

  int batch_write_fd;

  int max_epoll_events;
  int num_epoll_events;
  struct epoll_event* epoll_events;

  nxe_event* first;
  nxe_event* last;
  nxe_timer_queue timers[NXE_NUMBER_OF_TIMER_QUEUES];

  nxe_publisher gc_pub; // subscribe for gc events

  nxp_pool free_event_pool;
  nxp_chunk free_event_pool_initial_chunk;
  // chunk data to immediately follow here!!!
  char free_events[(sizeof(nxp_object)+sizeof(nxe_event))*NXE_FREE_EVENT_POOL_INITIAL_SIZE];
} nxe_loop;

#define ISTREAM_CLASS(is) ((is)->super.cls.is_cls)
#define OSTREAM_CLASS(os) ((os)->super.cls.os_cls)
#define TIMER_CLASS(t) ((t)->super.cls.timer_cls)
#define PUBLISHER_CLASS(pub) ((pub)->super.cls.pub_cls)
#define SUBSCRIBER_CLASS(sub) ((sub)->super.cls.sub_cls)

#define OBJ_PTR_FROM_FLD_PTR(obj_type, fld_name, fld_ptr) ((obj_type*)((char*)(fld_ptr)-offsetof(obj_type, fld_name)))

nxe_loop* nxe_create(int max_epoll_events);
void nxe_destroy(nxe_loop* loop);

void nxe_run(nxe_loop* loop);
void nxe_break(nxe_loop* loop);
void nxe_unref(nxe_loop* loop);

//void nxe_register_publisher(nxe_loop* loop, nxe_publisher* pub); // creates new nxe_event
//void nxe_unregister_publisher(nxe_publisher* pub); // removes nxe_event
void nxe_subscribe(nxe_loop* loop, nxe_publisher* pub, nxe_subscriber* sub);
void nxe_unsubscribe(nxe_publisher* pub, nxe_subscriber* sub);
void nxe_publish(nxe_publisher* pub, nxe_data data);

void nxe_register_eventfd_source(nxe_loop* loop, nxe_eventfd_source* efs); // registers with epoll
void nxe_unregister_eventfd_source(nxe_eventfd_source* efs); // unregisters with epoll
void nxe_trigger_eventfd(nxe_eventfd_source* efs); // wake up from other thread via eventfd/pipe; efs must be registered

void nxe_register_listenfd_source(nxe_loop* loop, nxe_listenfd_source* lfs); // registers with epoll
void nxe_unregister_listenfd_source(nxe_listenfd_source* lfs); // unregisters with epoll

void nxe_register_fd_source(nxe_loop* loop, nxe_fd_source* fs); // registers with epoll
void nxe_unregister_fd_source(nxe_fd_source* fs); // unregister from epoll
void nxe_connect_streams(nxe_loop* loop, nxe_istream* is, nxe_ostream* os);
void nxe_disconnect_streams(nxe_istream* is, nxe_ostream* os);

void nxe_schedule_callback(nxe_loop* loop, void (*func)(nxe_data data), nxe_data data); // creates new nxe_event

void nxe_set_timer_queue_timeout(nxe_loop* loop, int queue_idx, nxe_time_t usec_interval);
void nxe_set_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer);
void nxe_unset_timer(nxe_loop* loop, int queue_idx, nxe_timer* timer);

time_t nxe_get_current_http_time(nxe_loop* loop);
const char* nxe_get_current_http_time_str(nxe_loop* loop);
const char* nxe_get_current_iso8601_time_str(nxe_loop* loop);

static inline nxe_time_t nxe_get_time_usec() {
  struct timespec ts;
#ifdef CLOCK_MONOTONIC_RAW
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#else
  clock_gettime(CLOCK_MONOTONIC, &ts);
#endif
  return (nxe_time_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
}

void nxe_link(nxe_loop* loop, nxe_event* evt); // internal use only

static inline void nxe_istream_set_ready(nxe_loop* loop, nxe_istream* is) {
  if (is->ready) return;
  is->ready=1;
  nxe_ostream* os=is->pair;
  if (os && os->ready && !is->evt.loop) {
    is->evt.receiver.os=os;
    nxe_link(loop, &is->evt);
  }
}

static inline void nxe_istream_unset_ready(nxe_istream* is) {
  is->ready=0;
}

static inline void nxe_ostream_set_ready(nxe_loop* loop, nxe_ostream* os) {
  os->ready=1;
  nxe_istream* is=os->pair;
  if (is && is->ready && !is->evt.loop) {
    is->evt.receiver.os=os;
    nxe_link(loop, &is->evt);
  }
}

static inline void nxe_ostream_unset_ready(nxe_ostream* os) {
  os->ready=0;
}

static inline void nxe_init_event(nxe_event* evt) {
  memset(evt, 0, sizeof(nxe_event));
}

static inline void nxe_init_fd_source(nxe_fd_source* fs, int fd,
        const nxe_istream_class* is_cls, const nxe_ostream_class* os_cls,
        const nxe_publisher_class* err_pub_cls) {
  //assert(!fs->data_is.evt.loop);
  memset(fs, 0, sizeof(nxe_fd_source));
  fs->cls=NXE_ES_FD;
  fs->fd=fd;
  fs->data_is.super.cls.is_cls=is_cls;
  fs->data_is.evt.cls=NXE_EV_STREAM;
  fs->data_os.super.cls.os_cls=os_cls;
  fs->data_error.super.cls.pub_cls=err_pub_cls;
  fs->data_is.part_of_fd_source=1;
  fs->data_os.part_of_fd_source=1;
  fs->data_error.part_of_fd_source=1;
}

int nxe_eventfd_open(int* fd);
void nxe_eventfd_close(int* fd);

static inline void nxe_init_eventfd_source(nxe_eventfd_source* efs, const nxe_publisher_class* notify_pub_cls) {
  memset(efs, 0, sizeof(nxe_eventfd_source));
  nxe_eventfd_open(efs->fd);
  efs->cls=NXE_ES_EFD;
  efs->data_notify.super.cls.pub_cls=notify_pub_cls;
  efs->data_notify.part_of_efd_source=1;
}

static inline void nxe_finalize_eventfd_source(nxe_eventfd_source* efs) {
  nxe_eventfd_close(efs->fd);
}

static inline void nxe_init_listenfd_source(nxe_listenfd_source* lfs, int fd, const nxe_publisher_class* notify_pub_cls) {
  memset(lfs, 0, sizeof(nxe_listenfd_source));
  lfs->cls=NXE_ES_LFD;
  lfs->fd=fd;
  lfs->data_notify.super.cls.pub_cls=notify_pub_cls;
  lfs->data_notify.part_of_lfd_source=1;
}

static inline void nxe_init_timer(nxe_timer* t, const nxe_timer_class* cls) {
  memset(t, 0, sizeof(nxe_timer));
  t->super.cls.timer_cls=cls;
}

static inline void nxe_init_istream(nxe_istream* is, const nxe_istream_class* cls) {
  memset(is, 0, sizeof(nxe_istream));
  is->super.cls.is_cls=cls;
  is->evt.cls=NXE_EV_STREAM;
}

static inline void nxe_init_ostream(nxe_ostream* os, const nxe_ostream_class* cls) {
  memset(os, 0, sizeof(nxe_ostream));
  os->super.cls.os_cls=cls;
}

static inline void nxe_init_publisher(nxe_publisher* pub, const nxe_publisher_class* cls) {
  memset(pub, 0, sizeof(nxe_publisher));
  pub->super.cls.pub_cls=cls;
}

static inline void nxe_init_subscriber(nxe_subscriber* sub, const nxe_subscriber_class* cls) {
  memset(sub, 0, sizeof(nxe_subscriber));
  sub->super.cls.sub_cls=cls;
}

#ifdef	__cplusplus
}
#endif

#endif	/* NX_EVENT_H */

