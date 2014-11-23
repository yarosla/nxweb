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
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>

#include "misc.h"

void nxweb_open_log_file(char const* log_file, gid_t gid, uid_t uid) {
  int fd=open(log_file, O_WRONLY|O_CREAT|O_APPEND, 0664);
  if (fd==-1) {
    nxweb_die("open log_file %s failed", log_file);
  }
  if (uid && gid) {
    if (fchown(fd, uid, gid)) {
      nxweb_log_error("unable to chown error log file [%d]", errno);
      // non-fatal => continue
    }
  }
  int zfd=open("/dev/null", O_RDONLY);
  if (zfd==-1) {
    nxweb_die("open(/dev/null) failed");
  }
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  if (dup2(zfd, STDIN_FILENO)==-1 || dup2(fd, STDOUT_FILENO)==-1 || dup2(fd, STDERR_FILENO)==-1) {
    nxweb_die("dup2(stdin/stdout/stderr) failed");
  }
  close(fd);
  close(zfd);
  nxweb_log_error("=== LOG OPENED ===");
}

void nxweb_continue_as_daemon(char const* work_dir, char const* log_file, gid_t gid, uid_t uid) {
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
  if (log_file) nxweb_open_log_file(log_file, gid, uid);
}

void nxweb_create_pid_file(char const* pid_file, __pid_t pid, gid_t gid, uid_t uid) {
  char buf[32];
  const char* pid_str=uint_to_decimal_string(pid, buf, sizeof(buf));
  int fd=open(pid_file, O_CREAT|O_TRUNC|O_WRONLY, 0666);
  if (fd==-1) {
    nxweb_log_error("can't create pid file %s [%d]", pid_file, errno);
    return;
  }
  if (uid && gid) {
    if (fchown(fd, uid, gid)) {
      nxweb_log_error("unable to chown pid file [%d]", errno);
      // non-fatal => continue
    }
  }
  if (write(fd, pid_str, strlen(pid_str))) ;
  close(fd);
}

int nxweb_relauncher(void (*main_func)(), const char* pid_file) {
  pid_t pid=fork();

  if (pid<0) {
    exit(EXIT_FAILURE);
  }
  else if(pid>0) { // we are the parent
    int status;

    if (pid_file) nxweb_create_pid_file(pid_file, pid, 0, 0);
    if (waitpid(pid, &status, 0)==-1) {
      nxweb_log_error("waitpid failure");
      exit(EXIT_FAILURE);
    }
    if (pid_file) unlink(pid_file);

    if (WIFEXITED(status)) {
      nxweb_log_error("Server exited, status=%d", WEXITSTATUS(status));
      return WEXITSTATUS(status);
    }
    else if (WIFSIGNALED(status)) {
      nxweb_log_error("Server killed by signal %d", WTERMSIG(status));
      return 1;
    }
  }
  else { // we are the child
    main_func();
    exit(EXIT_SUCCESS);
  }
  return 0;
}

int nxweb_shutdown_daemon(const char* work_dir, const char* pid_file) {
  if (work_dir && chdir(work_dir)<0) {
    nxweb_die("chdir(work_dir) failed");
  }
  if (pid_file) {
    int fd=open(pid_file, O_RDONLY);
    if (fd!=-1) {
      char pid_str[20];
      pid_t pid=0;
      int len=read(fd, pid_str, sizeof(pid_str)-1);
      if (len>0) {
        pid_str[len]='\0';
        pid=strtol(pid_str, 0, 10);
      }
      close(fd);
      //unlink(pid_file);
      if (pid) {
        kill(pid, SIGTERM);
        return EXIT_SUCCESS;
      }
    }
  }
  fprintf(stderr, "Could not find PID file %s of running NXWEB\n", pid_file);
  return EXIT_FAILURE;
}

uid_t nxweb_get_uid_by_name(const char* user_name) {
  if (!user_name) return -1;
  long buflen=sysconf(_SC_GETPW_R_SIZE_MAX);
  if (buflen==-1) return -1;
  char* buf=malloc(buflen);
  struct passwd pwbuf, *pwbufp;
  getpwnam_r(user_name, &pwbuf, buf, buflen, &pwbufp);
  free(buf);
  return pwbufp? pwbufp->pw_uid : -1;
}

gid_t nxweb_get_gid_by_name(const char* group_name) {
  if (!group_name) return -1;
  long buflen=sysconf(_SC_GETGR_R_SIZE_MAX);
  if (buflen==-1) return -1;
  char* buf=malloc(buflen);
  struct group grbuf, *grbufp;
  getgrnam_r(group_name, &grbuf, buf, buflen, &grbufp);
  free(buf);
  return grbufp? grbufp->gr_gid : -1;
}

int nxweb_drop_privileges(gid_t gid, uid_t uid) {
  if (gid && uid) {
    // try chown'ing error log file
    // it might be still owned by root if privileges defined in conf file, not on command line
    if (fchown(STDERR_FILENO, gid, uid)) /* ignore */;
    // change them permanently
    if (setresgid(gid, gid, gid)==-1) {
      nxweb_log_error("can't set gid=%d errno=%d", gid, errno);
      return -1;
    }
    if (setresuid(uid, uid, uid)==-1) {
      nxweb_log_error("can't set uid=%d errno=%d", uid, errno);
      return -1;
    }
    nxweb_log_error("privileges dropped to gid=%d uid=%d", (int)gid, (int)uid);
  }
  return 0;
}

int nxweb_run_daemon(char const* work_dir, char const* log_file, char const* pid_file, void (* main_func)(), gid_t gid, uid_t uid) {
  nxweb_continue_as_daemon(work_dir, log_file, gid, uid);
  while (nxweb_relauncher(main_func, pid_file)) sleep(2);  // sleep 2 sec and launch again until child exits with EXIT_SUCCESS
  return EXIT_SUCCESS;
}

int nxweb_run_normal(char const* work_dir, char const* log_file, char const* pid_file, void (* main_func)(), gid_t gid, uid_t uid) {
  if (work_dir && chdir(work_dir)<0) {
    nxweb_die("chdir(work_dir) failed");
  }
  if (pid_file) nxweb_create_pid_file(pid_file, getpid(), gid, uid);
  if (log_file) nxweb_open_log_file(log_file, gid, uid);
  main_func();
  if (pid_file) unlink(pid_file); // this might not always succeed since the privileges are dropped
  return EXIT_SUCCESS;
}
