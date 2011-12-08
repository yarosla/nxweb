#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "nx_event.h"
#include "nx_buffer.h"
#include "misc.h"

enum {
  NXE_CLASS_LISTEN=0,
  NXE_CLASS_SOCKET
};

typedef struct nxweb_connection {
  nxe_timer timer_keep_alive;

  nxb_buffer iobuf;
  char buf[5120];
} nxweb_connection;

static const char* response = "HTTP/1.1 200 OK\r\n"
                              "Connection: keep-alive\r\n"
                              "Content-Length: 20\r\n"
                              "Content-Type: text/plain\r\n"
                              "\r\n"
                              "<p>Hello, world!</p>";

static void on_keep_alive(nxe_loop* loop, void* _evt) {
  nxe_event* evt=_evt;
  nxweb_connection* conn=NXE_EVENT_DATA(evt);

  nxweb_log_error("[%p] keep-alive socket closed", conn);
  nxe_stop(loop, evt);
  _nxweb_close_bad_socket(evt->fd);
  nxe_delete_event(loop, evt);
}

static void on_new_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;

  nxweb_log_error("[%p] new conn", conn);

  conn->timer_keep_alive.callback=on_keep_alive;
  conn->timer_keep_alive.data=evt;
  nxe_set_timer(loop, &conn->timer_keep_alive, 10000000);

  nxb_init(&conn->iobuf, sizeof(conn->iobuf)+sizeof(conn->buf));

  evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
  evt->read_status=NXE_CAN_AND_WANT;
}

static void on_delete_connection(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  nxe_unset_timer(loop, &conn->timer_keep_alive);
  nxb_empty(&conn->iobuf);
}

static void on_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
//  nxweb_log_error("on_error");
  nxe_stop(loop, evt);
  if (events & NXE_ERROR) {
    nxweb_log_error("on_error! %d", events);
    _nxweb_close_bad_socket(evt->fd);
  }
  else {
    //nxweb_log_error("on_error %d", events);
    _nxweb_close_bad_socket(evt->fd);
  }
  nxe_delete_event(loop, evt);
}

static void on_read(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;
  if (events&NXE_READ_FULL) {
    // TODO: header too long
    nxweb_log_error("header too long");
    return;
  }

  nxe_unset_timer(loop, &conn->timer_keep_alive);

  char* read_buf=nxb_get_unfinished(&conn->iobuf, 0);
  int size=evt->read_ptr - read_buf;
  nxb_blank(&conn->iobuf, size);
  read_buf[size]='\0';
  if (strstr(read_buf, "\r\n\r\n")) {
    nxweb_log_error("[%p] request received", conn);
    nxb_finish_obj(&conn->iobuf);
    evt->read_status&=~NXE_WANT;
    evt->write_status=NXE_CAN_AND_WANT;
    evt->write_ptr=response;
    evt->write_size=strlen(response);
  }
}

static void on_write(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_connection* conn=evt_data;

  if (events & NXE_WRITE_FULL) {
    nxb_empty(&conn->iobuf);
    evt->read_ptr=nxb_get_room(&conn->iobuf, &evt->read_size);
    evt->write_status&=~NXE_WANT;
    evt->read_status=NXE_CAN_AND_WANT;
    nxe_set_timer(loop, &conn->timer_keep_alive, 10000000);
  }
}

static void on_accept(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  int client_fd;
  struct sockaddr_in client_addr;
  socklen_t client_len=sizeof(client_addr);
  while ((client_fd=accept(evt->fd, (struct sockaddr *)&client_addr, &client_len))!=-1) {
    if (_nxweb_set_non_block(client_fd) || _nxweb_setup_client_socket(client_fd)) {
      _nxweb_close_bad_socket(client_fd);
      nxweb_log_error("failed to setup client socket");
      continue;
    }
    nxe_event* cevt=nxe_new_event_fd(loop, NXE_CLASS_SOCKET, client_fd);
    nxe_start(loop, cevt);
  }
  char buf[1024];
  strerror_r(errno, buf, sizeof(buf));
  if (errno==EAGAIN) {
    evt->read_status&=~NXE_CAN;
  }
  else {
    nxweb_log_error("accept() returned -1: errno=%d %s", errno, buf);
  }
}

static void on_accept_error(nxe_loop* loop, nxe_event* evt, void* evt_data, int events) {
  nxweb_log_error("on_accept_error");
}

static int listen_fd;

static void* net_thread_main(void* pdata) {
  nxe_loop* loop=nxe_create(sizeof(nxweb_connection), 128);
  nxe_event* evt;

  evt=nxe_new_event_fd(loop, NXE_CLASS_LISTEN, listen_fd);
  evt->read_status=NXE_CAN_AND_WANT;

  nxe_start(loop, evt);

  nxe_run(loop);
  return 0;
}

#define NTHREADS 1

nxe_event_class nxe_event_classes[]={
  {.on_read=on_accept, .on_error=on_accept_error},
  {.on_read=on_read, .on_write=on_write, .on_error=on_error,
   .on_new=on_new_connection, .on_delete=on_delete_connection}
};

/*
static void test_nx_buffer() {
  int i;
  nxb_buffer* nxb=nxb_create(111);
  for (i=0; i<20; i++) nxb_append(nxb, "test", 4);
  void* p1=nxb_alloc_obj(nxb, 64);
  void* p2=nxb_finish_obj(nxb);
  for (i=0; i<2000; i++) nxb_append(nxb, "TEST", 4);
  void* p3=nxb_alloc_obj(nxb, 6400);
  void* p4=nxb_finish_obj(nxb);
  nxb_destroy(nxb);
}
*/

// Signal server to shutdown. Async function. Can be called from worker threads.
void nxweb_shutdown() {
}

void _nxweb_main() {
  nxweb_log_error("loop[%d] evt[%d] %ld\n", (int)sizeof(nxe_loop), (int)sizeof(nxe_event), nxe_get_time_usec());

  listen_fd=_nxweb_bind_socket(8777);

  pthread_t tids[NTHREADS];
  int i;
  for (i=0; i<NTHREADS; i++) {
    pthread_create(&tids[i], 0, net_thread_main, 0);
  }
  //nxweb_log_error("started at %ld [%d]\n", nxe_get_time_usec(), (int)sizeof(nxe_time_t));
  for (i=0; i<NTHREADS; i++) {
    pthread_join(tids[i], 0);
  }

  close(listen_fd);
}
