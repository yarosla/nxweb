/*
 * Copyright (c) 2011 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <argp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "nxweb_internal.h"

static void open_log_file(const char* log_file) {
  int fd=open(log_file, O_WRONLY|O_CREAT|O_APPEND, 0664);
  if (fd==-1) {
    nxweb_die("open(log_file) failed");
  }
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if (dup2(fd, STDOUT_FILENO)==-1 || dup2(fd, STDERR_FILENO)==-1) {
    nxweb_die("dup2(stdout/err) failed");
  }
  close(fd);
}

static void continue_as_daemon(const char* work_dir, const char* log_file) {
  /* Our process ID and Session ID */
  pid_t pid, sid;

  /* Fork off the parent process */
  pid=fork();
  if (pid<0) {
    exit(EXIT_FAILURE);
  }
  /* If we got a good PID, then we can exit the parent process. */
  if (pid>0) {
    exit(EXIT_SUCCESS);
  }

  /* Create a new SID for the child process */
  sid=setsid();
  if (sid<0) {
    nxweb_die("setsid() failed");
  }

  /* Change the file mode mask */
  umask(0);

  /* Change the current working directory */
  if (work_dir && chdir(work_dir)<0) {
    nxweb_die("chdir(work_dir) failed");
  }

  /* Close out the standard file descriptors */
//  close(STDIN_FILENO);
//  close(STDOUT_FILENO);
//  close(STDERR_FILENO);
  open_log_file(log_file);
}

static int launcher(void (*main_func)()) {
  pid_t pid=fork();

  if (pid<0) {
    exit(EXIT_FAILURE);
  } else if(pid>0) { // we are the parent
    int status;

    if (waitpid(pid, &status, 0)==-1) {
      nxweb_log_error("waitpid failure");
      exit(EXIT_FAILURE);
    }

    if (WIFEXITED(status)) {
      nxweb_log_error("Server exited, status=%d", WEXITSTATUS(status));
      return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      nxweb_log_error("Server killed by signal %d", WTERMSIG(status));
      return 1;
    }
  } else { // we are the child
    main_func();
    exit(EXIT_SUCCESS);
  }
  return 0;
}

const char *argp_program_version="nxweb " REVISION;
const char *argp_program_bug_address="<win@nexoft.ru>";

/* Program documentation. */
static char doc[]="NXWEB - ultra-fast and super-lightweight web server.";

/* A description of the arguments we accept. */
static char args_doc[] = "";

/* The options we understand. */
static struct argp_option options[] = {
  { "shutdown", 's', 0, 0, "Shutdown server" },
  { "daemon", 'd', 0, 0, "Run as daemon" },
  { "work-dir", 'w', "DIR", 0, "Change dir to specified" },
  { "log-file", 'l', "FILE", 0, "Specify log file (error.log by default)" },
  { 0 }
};

/* Used by main to communicate with parse_opt. */
struct arguments {
  int daemon;
  int shutdown;
  const char* work_dir;
  const char* log_file;
};

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments=state->input;

  switch (key) {
    case 's':
      arguments->shutdown=1;
      break;
    case 'd':
      arguments->daemon=1;
      break;
    case 'w':
      arguments->work_dir=arg;
      break;

    case ARGP_KEY_ARG:
    case ARGP_KEY_END:
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

/* Our argp parser. */
static struct argp argp={ options, parse_opt, args_doc, doc };

int main(int argc, char* argv[]) {
  struct arguments args;
  args.daemon=0;
  args.shutdown=0;
  args.work_dir=0;
  args.log_file=ERROR_LOG_FILE;

  argp_parse(&argp, argc, argv, 0, 0, &args);

  if (args.shutdown) {
    if (args.work_dir && chdir(args.work_dir)<0) {
      nxweb_die("chdir(work_dir) failed");
    }
    FILE* f=fopen(NXWEB_PID_FILE, "r");
    if (f) {
      char pid_str[20];
      pid_t pid=0;
      if (fgets(pid_str, sizeof(pid_str), f))
        pid=strtol(pid_str, 0, 10);
      fclose(f);
      if (pid) {
        kill(pid, SIGTERM);
        unlink(NXWEB_PID_FILE);
        return EXIT_SUCCESS;
      }
    }
    fprintf(stderr, "Could not find PID of running %s\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (args.daemon) {
    continue_as_daemon(args.work_dir, args.log_file);
    while (launcher(_nxweb_main)) sleep(2);  // sleep 2 sec and launch again until child exits with EXIT_SUCCESS
  }
  else {
    if (args.work_dir && chdir(args.work_dir)<0) {
      nxweb_die("chdir(work_dir) failed");
    }
    open_log_file(args.log_file);
    _nxweb_main();
  }

  return EXIT_SUCCESS;
}
