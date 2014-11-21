/*
 * Copyright (c) 2014 Yaroslav Stavnichiy <yarosla@gmail.com>
 */

#include "nxweb/nxweb.h"


static void on_net_thread_diagnostics() {
  nxweb_net_thread_data* tdata=_nxweb_net_thread_data;
  nxe_loop* loop=tdata->loop;
  nxe_time_t current_time=loop->current_time;

  // nxweb_log_error("net thread diagnostics begin");

  nxp_pool_iterator itr;
  nxweb_http_server_connection* conn;
  for (conn=nxp_iterate_allocated_objects(tdata->free_conn_pool, &itr); conn; conn=nxp_iterate_allocated_objects(0, &itr)) {
    nxweb_log_error("[diag] conn %p %ds %s [%d] parent=%p state=%d %s", conn, (int)((current_time-conn->connected_time)/1000000),
        conn->remote_addr, conn->hsp.request_count, conn->parent, (int)conn->hsp.state, conn->hsp.req.uri);
  }

  // nxweb_log_error("net thread diagnostics end");
}

NXWEB_MODULE(diag_connections, .on_thread_diagnostics=on_net_thread_diagnostics);
