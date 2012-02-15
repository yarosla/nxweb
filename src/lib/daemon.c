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

#include <stdlib.h>
#include <stdio.h>
#include <argp.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "misc.h"

void nxweb_open_log_file(const char* log_file) {
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

void nxweb_continue_as_daemon(const char* work_dir, const char* log_file) {
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
  if (log_file) nxweb_open_log_file(log_file);
}

void nxweb_create_pid_file(const char* pid_file) {
  FILE* f=fopen(pid_file, "w");
  if (f) {
    fprintf(f, "%d", (int)getpid());
    fclose(f);
  }
}

int nxweb_relauncher(void (*main_func)(), const char* pid_file) {
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
    if (pid_file) nxweb_create_pid_file(pid_file);
    main_func();
    if (pid_file) unlink(pid_file);
    exit(EXIT_SUCCESS);
  }
  return 0;
}

int nxweb_shutdown_daemon(const char* work_dir, const char* pid_file) {
  if (work_dir && chdir(work_dir)<0) {
    nxweb_die("chdir(work_dir) failed");
  }
  FILE* f=fopen(pid_file, "r");
  if (f) {
    char pid_str[20];
    pid_t pid=0;
    if (fgets(pid_str, sizeof(pid_str), f))
      pid=strtol(pid_str, 0, 10);
    fclose(f);
    if (pid) {
      kill(pid, SIGTERM);
      unlink(pid_file);
      return EXIT_SUCCESS;
    }
  }
  fprintf(stderr, "Could not find PID file %s of running NXWEB\n", pid_file);
  return EXIT_FAILURE;
}

int nxweb_run_daemon(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)()) {
  nxweb_continue_as_daemon(work_dir, log_file);
  while (nxweb_relauncher(main_func, pid_file)) sleep(2);  // sleep 2 sec and launch again until child exits with EXIT_SUCCESS
  return EXIT_SUCCESS;
}

int nxweb_run_normal(const char* work_dir, const char* log_file, const char* pid_file, void (*main_func)()) {
  if (work_dir && chdir(work_dir)<0) {
    nxweb_die("chdir(work_dir) failed");
  }
  if (log_file) nxweb_open_log_file(log_file);
  if (pid_file) nxweb_create_pid_file(pid_file);
  main_func();
  if (pid_file) unlink(pid_file);
  return EXIT_SUCCESS;
}
