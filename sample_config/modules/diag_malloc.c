/*
 * Copyright (c) 2014 Yaroslav Stavnichiy <yarosla@gmail.com>
 */

#define _GNU_SOURCE

#include "nxweb/nxweb.h"

#include <execinfo.h>
#include <inttypes.h>
#include <malloc.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

void *__libc_malloc(size_t size);
void *__libc_valloc(size_t size);
void *__libc_pvalloc(size_t size);
void *__libc_calloc(size_t n, size_t size);
void *__libc_realloc(void *ptr, size_t size);
void *__libc_memalign(size_t alignment, size_t size);
void __libc_free (void *ptr);

#define RETURN_ADDRESS(nr) __builtin_extract_return_addr(__builtin_return_address(nr))

typedef struct nx_meminfo {
  size_t size;
  nxe_time_t tim;
  // nxweb_net_thread_data* tdata;
  void* bt[4];
  struct nx_meminfo* prev;
  struct nx_meminfo* next;
  uint64_t conn_uid;
  uint64_t req_uid;
  char req_uri[40];
  char sig;
  char fn;
  uint8_t net_thread_num;
  _Bool worker:1;
  uint32_t checksum;
} nx_meminfo __attribute__ ((aligned(64)));

static long nmalloc=0;
static long nfree=0;
static nxe_time_t server_started_time;

#define SIZE_OF_MEMINFO (sizeof(nx_meminfo)+sizeof(uint32_t))
#define FN_MALLOC 'm'
#define FN_CALLOC 'c'
#define FN_REALLOC 'r'
#define FN_VALLOC 'v'
#define FN_PVALLOC 'p'
#define FN_MEMALIGN 'g'
#define FN_POSIX_MEMALIGN 'G'
#define MEMINFO_SIG '>'
#define ROOT_SIG '!'
#define INC_MALLOC __sync_add_and_fetch(&nmalloc, 1)
#define INC_FREE __sync_add_and_fetch(&nfree, 1)

#define MAX_MI_REPORTS 256
#define LARGE_MI_THRESHOLD 4096
#define PARANOIC 0

static nx_meminfo mi_root={.sig=ROOT_SIG, .prev=&mi_root, .next=&mi_root};

static pthread_mutex_t mi_mux=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP; // PTHREAD_MUTEX_INITIALIZER;

static uint32_t fletcher32(uint16_t const *data, size_t words) {
  uint32_t sum1 = 0xffff, sum2 = 0xffff;
  while (words) {
    unsigned tlen = words > 359 ? 359 : words;
    words -= tlen;
    do {
      sum2 += sum1 += *data++;
    } while (--tlen);
    sum1 = (sum1 & 0xffff) + (sum1 >> 16);
    sum2 = (sum2 & 0xffff) + (sum2 >> 16);
  }
  /* Second reduction step to reduce sums to 16 bits */
  sum1 = (sum1 & 0xffff) + (sum1 >> 16);
  sum2 = (sum2 & 0xffff) + (sum2 >> 16);
  return sum2 << 16 | sum1;
}

static void set_checksum(nx_meminfo* mi) {
  uint32_t* endp=(uint32_t*)((char*)(mi+1)+mi->size);
  *endp=mi->checksum=fletcher32((uint16_t*)mi, offsetof(nx_meminfo, checksum)/2);
}

static _Bool verify_checksum(nx_meminfo* mi, _Bool lock) {
  if (mi==&mi_root) return 1; // root is always valid
  if (lock) pthread_mutex_lock(&mi_mux);
  uint32_t* endp=(uint32_t*)((char*)(mi+1)+mi->size);
  uint32_t checksum=fletcher32((uint16_t*)mi, offsetof(nx_meminfo, checksum)/2);
  _Bool result=mi->checksum==checksum && *endp==checksum;
  if (lock) pthread_mutex_unlock(&mi_mux);
  return result;
}

static void link_mi(nx_meminfo* mi) {
  int res=pthread_mutex_lock(&mi_mux);
  assert(res==0 && !mi->next && !mi->prev);
#if PARANOIC
  nx_meminfo* _mi;
  for (_mi=mi_root.next; _mi!=&mi_root; _mi=_mi->next) {
    if (!_mi || !_mi->next || !_mi->prev || !verify_checksum(_mi, 0)) raise(SIGABRT);
  }
#endif
  mi->prev=&mi_root;
  mi->next=mi_root.next;
  // assert(verify_checksum(mi->next, 0));
  mi->prev->next=mi;
  mi->next->prev=mi;
  set_checksum(mi);
  set_checksum(mi->next);
  // no need to set_checksum(mi->prev) as mi->prev is root
#if PARANOIC
  for (_mi=mi_root.next; _mi!=&mi_root; _mi=_mi->next) {
    if (!_mi || !_mi->next || !_mi->prev || !verify_checksum(_mi, 0)) raise(SIGABRT);
  }
#endif
  pthread_mutex_unlock(&mi_mux);
}

static void unlink_mi(nx_meminfo* mi) {
  int res=pthread_mutex_lock(&mi_mux);
  assert(!res && mi && mi->next && mi->prev);
#if PARANOIC
  nx_meminfo* _mi;
  for (_mi=mi_root.next; _mi!=&mi_root; _mi=_mi->next) {
    if (!_mi || !_mi->next || !_mi->prev || !verify_checksum(_mi, 0)) raise(SIGABRT);
  }
#endif
  mi->prev->next=mi->next;
  mi->next->prev=mi->prev;
  set_checksum(mi->prev);
  set_checksum(mi->next);
  mi->prev=mi->next=0;
#if PARANOIC
  for (_mi=mi_root.next; _mi!=&mi_root; _mi=_mi->next) {
    if (!_mi || !_mi->next || !_mi->prev || !verify_checksum(_mi, 0)) raise(SIGABRT);
  }
#endif
  pthread_mutex_unlock(&mi_mux);
}

static void* fill_meminfo(nx_meminfo* mi, size_t size, char fn, void* caller) {
  if (!mi) return 0;
  memset(mi, 0, sizeof(nx_meminfo));
  mi->size=size;
  mi->fn=fn;
  mi->sig=MEMINFO_SIG;

  // void* bt[5];
  // int nbt=backtrace(bt, 5);
  // memcpy(mi->bt, bt+1, (nbt-1)*sizeof(void*));

  mi->bt[0]=caller;
  // mi->bt[1]=mi->bt[0]? RETURN_ADDRESS(2) : 0;
  // mi->bt[2]=mi->bt[1]? RETURN_ADDRESS(3) : 0;
  // mi->bt[3]=RETURN_ADDRESS(4);

  nxweb_net_thread_data* tdata=_nxweb_net_thread_data;
  nxweb_http_server_connection* conn;
  if (!tdata) {
    nxw_worker* w=_nxweb_worker_thread_data;
    if (w) {
      int i;
      mi->worker=1;
      for (i=0; i<_nxweb_num_net_threads; i++) {
        if (w->factory==&_nxweb_net_threads[i].workers_factory) {
          tdata=&_nxweb_net_threads[i];
          break;
        }
      }
      conn=w->job_param;
      mi->conn_uid=conn->uid;
      mi->req_uid=conn->hsp.req.uid;
      strncpy(mi->req_uri, conn->hsp.req.uri, sizeof(mi->req_uri)-1);
    }
  }
  if (tdata) {
    mi->net_thread_num=tdata->thread_num+1;
    mi->tim=tdata->loop? tdata->loop->current_time : 0;
  }
  if (!mi->tim) {
    mi->tim=nxe_get_time_usec();
  }
  link_mi(mi);
  return mi+1;
}

void *malloc(size_t size) {
  INC_MALLOC;
  void* ptr=__libc_malloc(size+SIZE_OF_MEMINFO);
  return fill_meminfo(ptr, size, FN_MALLOC, RETURN_ADDRESS(0));
}

void *valloc(size_t size) {
  INC_MALLOC;
  void* ptr=__libc_valloc(size+SIZE_OF_MEMINFO);
  return fill_meminfo(ptr, size, FN_VALLOC, RETURN_ADDRESS(0));
}

void *pvalloc(size_t size) {
  INC_MALLOC;
  void* ptr=__libc_pvalloc(size+SIZE_OF_MEMINFO);
  return fill_meminfo(ptr, size, FN_PVALLOC, RETURN_ADDRESS(0));
}

void *calloc(size_t n, size_t size) {
  INC_MALLOC;
  void* ptr=__libc_calloc(1, n*size+SIZE_OF_MEMINFO);
  return fill_meminfo(ptr, n*size, FN_CALLOC, RETURN_ADDRESS(0));
}

void *memalign(size_t alignment, size_t size) {
  INC_MALLOC;
  void* ptr=__libc_memalign(alignment, size+SIZE_OF_MEMINFO);
  return fill_meminfo(ptr, size, FN_MEMALIGN, RETURN_ADDRESS(0));
}

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  INC_MALLOC;
  void* ptr=__libc_memalign(alignment, size+SIZE_OF_MEMINFO);
  *memptr=fill_meminfo(ptr, size, FN_POSIX_MEMALIGN, RETURN_ADDRESS(0));
  return ptr? 0:ENOMEM;
}

void *realloc(void *ptr, size_t size) {
  if (ptr) {
    nx_meminfo* mi=ptr-sizeof(nx_meminfo);
    if (mi->sig==MEMINFO_SIG && verify_checksum(mi, 1)) {
      pthread_mutex_lock(&mi_mux);
      nx_meminfo* new_mi=__libc_realloc(mi, size+SIZE_OF_MEMINFO);
      new_mi->size=size;
      new_mi->next->prev=new_mi;
      new_mi->prev->next=new_mi;
      new_mi->fn=FN_REALLOC;
      set_checksum(new_mi);
      set_checksum(new_mi->prev);
      set_checksum(new_mi->next);
      pthread_mutex_unlock(&mi_mux);
      return new_mi+1;
    }
    else {
      fprintf(stderr, "!!!!!!!!!!!! reallocating non-marked memory %p\n", RETURN_ADDRESS(0));
      return __libc_realloc(ptr, size);
    }
  }
  else {
    INC_MALLOC;
    void* ptr=__libc_realloc(0, size+SIZE_OF_MEMINFO);
    return fill_meminfo(ptr, size, FN_REALLOC, RETURN_ADDRESS(0));
  }
}

void free(void *ptr) {
  static void* last_non_marked_free_callers[]={0,0,0};

  if (!ptr) {
    // raise(SIGABRT);
    // fprintf(stderr, "freeing null pointer %p\n", RETURN_ADDRESS(0));
    return;
  }
  nx_meminfo* mi=ptr-sizeof(nx_meminfo);
  if (mi->sig==MEMINFO_SIG && verify_checksum(mi, 1)) {
    INC_FREE;
    unlink_mi(mi);
    memset(ptr, 0xff, mi->size); // wipe memory so it cannot be used after free()
    __libc_free(mi);
  }
  else {
    // look through list
    pthread_mutex_lock(&mi_mux);
    nx_meminfo* _mi;
    for (_mi=mi_root.next; _mi!=&mi_root; _mi=_mi->next) {
      if (!_mi || !_mi->next || !_mi->prev || !verify_checksum(_mi, 0)) raise(SIGABRT);
      assert(_mi!=mi); // mi in list but corrupted
      // if (_mi==mi) raise(SIGABRT); // mi in list but corrupted
    }
    pthread_mutex_unlock(&mi_mux);
    void* caller=RETURN_ADDRESS(0);
    if (caller!=last_non_marked_free_callers[0]
        && caller!=last_non_marked_free_callers[1]
        && caller!=last_non_marked_free_callers[2]) {
      // do not print repeat same callers
      fprintf(stderr, "!!!!!!!!!!!! freeing non-marked memory %p\n", caller);
      last_non_marked_free_callers[2]=last_non_marked_free_callers[1];
      last_non_marked_free_callers[1]=last_non_marked_free_callers[0];
      last_non_marked_free_callers[0]=caller;
    }
    // raise(SIGABRT);
    __libc_free(ptr);
  }
}

static int on_server_startup() {
  server_started_time=nxe_get_time_usec();
  nxweb_log_error("[diag-malloc] sizeof(mi)=%d checksum_offset=%d",
      (int)sizeof(nx_meminfo), (int)offsetof(nx_meminfo, checksum));
  return NXWEB_OK;
}

static void on_server_diagnostics() {
  nxweb_log_error("[diag-malloc] nmalloc=%ld nfree=%ld", nmalloc, nfree);
  nx_meminfo* mi;
  nx_meminfo* mip[MAX_MI_REPORTS];
  int count=0, total=0, i, num_reports=0;
  size_t bytes=0, total_bytes=0;
  nxe_time_t now=nxe_get_time_usec();
  nxe_time_t start_time=server_started_time+60000000; // 300 sec = 5 min
  nxe_time_t end_time=now-30000000; // 30 sec
  int res=pthread_mutex_lock(&mi_mux);
  assert(!res);
  for (mi=mi_root.next; mi!=&mi_root && total<1000000; mi=mi->next) {
    assert(mi && mi->next && mi->prev);
    total++;
    total_bytes+=mi->size;
    if (mi->tim > start_time && mi->tim < end_time) {
      if (count < MAX_MI_REPORTS-64) // collect first reports unconditionally
        mip[num_reports++]=mi;
      else if (num_reports < MAX_MI_REPORTS && mi->size >= LARGE_MI_THRESHOLD) // report only large chunks at the end
        mip[num_reports++]=mi;
      count++;
      bytes+=mi->size;
    }
  }

  nxweb_log_error("[diag-malloc] %ldb out of %ldb (%ldb); %d out of %d blocks", bytes, total_bytes,
      total_bytes+(total*sizeof(nx_meminfo)), count, total);
  for (i=0; i<num_reports; i++) {
    mi=mip[i];
    char** bts=backtrace_symbols(mi->bt, 1);
    nxweb_log_error("[diag-malloc] mi %p %s %c %ldb%s %ds t=%d%c conn=%" PRIx64 " req=%" PRIx64 " \"%s\"",
        mi, bts[0], mi->fn, mi->size, (mi->size >= LARGE_MI_THRESHOLD?" !!!":""), (int)((now - mi->tim + 500000)/1000000),
        (int)mi->net_thread_num, mi->worker?'W':'_', mi->conn_uid, mi->req_uid, mi->req_uri);
    free(bts);
  }
  pthread_mutex_unlock(&mi_mux);
}

NXWEB_MODULE(diag_malloc,
    .on_server_startup=on_server_startup,
    .on_server_diagnostics=on_server_diagnostics
  );
