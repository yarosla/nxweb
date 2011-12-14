#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

#define NXWEB_LISTEN_PORT 8777
#define NXWEB_PID_FILE "nxweb.pid"

#ifdef NX_DEBUG
#define N_NET_THREADS 1
// N_WORKER_THREADS - number of worker threads per each network thread
#define N_WORKER_THREADS 1
#else
#define N_NET_THREADS 4
// N_WORKER_THREADS - number of worker threads per each network thread
#define N_WORKER_THREADS 4
#endif

#define NXWEB_ACCEPT_QUEUE_SIZE 16384
#define NXWEB_JOBS_QUEUE_SIZE 8192

#define NXWEB_STALE_EVENT_TIMEOUT 40000000
#define NXWEB_KEEP_ALIVE_TIMEOUT 30000000
#define NXWEB_WRITE_TIMEOUT 10000000
#define NXWEB_READ_TIMEOUT 10000000

#define REQUEST_CONTENT_SIZE_LIMIT 524288

#define NXWEB_MAX_RESPONSE_HEADERS 16

#endif // CONFIG_H_INCLUDED
