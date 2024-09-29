/*
 * Copyright (C) 2023 Benoit Baudaux
 *
 * This file is part of EXA.
 *
 * EXA is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * EXA is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with EXA. If not, see <https://www.gnu.org/licenses/>.
 */

#include "process.h"
#include "vfs.h"
#include "msg.h"

#include <string.h>
#include <signal.h>

#include <sys/timerfd.h>
#include <sys/wait.h>

#include <emscripten.h>

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
#else
#define emscripten_log(...)
#endif

#define NO_PARENT 0

#define RESMGR_ID 1
#define TTY_ID    2
#define NETFS_ID  3
#define PIPE_ID   4
#define INIT_ID   5

static struct process processes[NB_PROCESSES_MAX];
static int nb_processes = 0;

static struct vnode * vfs_proc;

pid_t process_fork(pid_t pid, pid_t ppid, const char * name);

void process_init() {

  struct vnode * vnode = vfs_find_node("/", NULL);

  // Add /proc
  vfs_proc = vfs_add_dir(vnode, "proc");
  
  for (int i = 0; i < NB_PROCESSES_MAX; ++i) {

    processes[i].pid = -1;
    processes[i].proc_state = EXITED_STATE;
  }

  process_fork(RESMGR_ID, NO_PARENT, "resmgr");
}

void add_proc_entry(pid_t pid) {

  char str[8];

  pid = pid & 0xffff;

  sprintf(str, "%d", pid);

  // Add /proc/<pid>
  struct vnode * vfs_proc_pid = vfs_add_dir(vfs_proc, str);

  // Add /proc/<pid>/fd
  struct vnode * vfs_proc_pid_fd = vfs_add_dir(vfs_proc_pid, "fd");
}

void del_proc_entry(pid_t pid) {

  char str[16];

  pid = pid & 0xffff;

  sprintf(str, "/proc/%d", pid);

  struct vnode * vnode = vfs_find_node(str, NULL);

  if (vnode)
    vfs_del_tree(vnode);
}

void process_add_proc_fd_entry(pid_t pid, int fd, char * link) {

  char str[32];
  char str2[8];

  pid = pid & 0xffff;

  sprintf(str, "/proc/%d/fd", pid);

  sprintf(str2, "%d", fd);

  struct vnode * vfs_proc_pid_fd = vfs_find_node(str, NULL);

  if (vfs_proc_pid_fd)
    vfs_add_symlink(vfs_proc_pid_fd, str2, link, NULL);
}

void process_del_proc_fd_entry(pid_t pid, int fd) {

  char str[32];

  pid = pid & 0xffff;

  sprintf(str, "/proc/%d/fd/%d", pid, fd);
  
  struct vnode * vfs_proc_pid_fd = vfs_find_node(str, NULL);

  if (vfs_proc_pid_fd)
    vfs_del_node(vfs_proc_pid_fd);
}

pid_t create_tty_process() {

  process_fork(TTY_ID, NO_PARENT, "tty");

  pid_t pid = fork();
  
  if (pid == -1) { // Error
    
    emscripten_log(EM_LOG_CONSOLE,"Error while creating tty process ...");
    
    return -1;
    
  } else if (pid == 0) { // Child process

    emscripten_log(EM_LOG_CONSOLE,"starting tty process...");

    execl ("/bin/tty", "/bin/tty", (void*)0);
    
  } else { // Parent process

    emscripten_log(EM_LOG_CONSOLE,"tty process created: %d",pid);

    return pid;
  }

  return 0;
}

pid_t create_netfs_process() {

  process_fork(NETFS_ID, NO_PARENT, "netfs");
  
  pid_t pid = fork();
  
  if (pid == -1) { // Error
    
    emscripten_log(EM_LOG_CONSOLE,"Error while creating netfs process ...");
    
    return -1;
    
  } else if (pid == 0) { // Child process

    emscripten_log(EM_LOG_CONSOLE,"starting netfs process...");

    execl ("/bin/netfs", "/bin/netfs", (void*)0);
    
  } else { // Parent process
    
    emscripten_log(EM_LOG_CONSOLE,"netfs process created: %d",pid);

    return pid;
  }

  return 0;
}

pid_t create_pipe_process() {

  process_fork(PIPE_ID, NO_PARENT, "pipe");
  
  pid_t pid = fork();
  
  if (pid == -1) { // Error
    
    emscripten_log(EM_LOG_CONSOLE,"Error while creating pipe process ...");
    
    return -1;
    
  } else if (pid == 0) { // Child process

    emscripten_log(EM_LOG_CONSOLE,"starting pipe process...");

    execl ("/bin/pipe", "/bin/pipe", (void*)0);
    
  } else { // Parent process
    
    emscripten_log(EM_LOG_CONSOLE,"pipe process created: %d",pid);

    return pid;
  }

  return 0;
}

pid_t create_init_process() {

  process_fork(INIT_ID, NO_PARENT, "init");
  
  pid_t pid = fork();
  
  if (pid == -1) { // Error
    
    emscripten_log(EM_LOG_CONSOLE,"Error while creating init process ...");
    
    return -1;
    
  } else if (pid == 0) { // Child process

    emscripten_log(EM_LOG_CONSOLE,"starting init process...");

    execl ("/bin/sysvinit", "/bin/sysvinit", "--init", (void*)0);
    
  } else { // Parent process
    
    emscripten_log(EM_LOG_CONSOLE, "init process created: %d", pid);

    return pid;
  }

  return 0;
}

// TODO : do not use pid as index

pid_t process_fork(pid_t pid, pid_t ppid, const char * name) {
  emscripten_log(EM_LOG_CONSOLE,"process_fork: %d %d", pid, ppid);
  
  if (pid < 0)
    pid = nb_processes;
  else {

    pid = pid & 0xffff;
    
    nb_processes = pid;
  }

  if (pid >= NB_PROCESSES_MAX)
    return -1;

  add_proc_entry(pid);
  
  processes[pid].proc_state = RUNNING_STATE;
  
  processes[pid].pid = pid;
  processes[pid].ppid = ppid;

  if (ppid >= 0) {

    emscripten_log(EM_LOG_CONSOLE,"(2) process_fork: %d %d %d %d", pid, ppid, processes[ppid].pgid, processes[ppid].sid);

    processes[pid].pgid =  processes[ppid].pgid;
    processes[pid].sid =  processes[ppid].sid;

    strcpy(processes[pid].cwd, processes[ppid].cwd);

    processes[pid].umask = processes[ppid].umask;
    memcpy(&processes[pid].sigprocmask, &processes[ppid].sigprocmask, sizeof(sigset_t));

    memcpy(&processes[pid].sigactions, &processes[ppid].sigactions, 32*sizeof(struct sigaction));
  }
  else {

    processes[pid].pgid = 0;
    processes[pid].sid = 0;

    strcpy(processes[pid].cwd, "");

    sigemptyset(&processes[pid].sigprocmask);

    memset(&processes[pid].sigactions, 0, 32*sizeof(struct sigaction));
  }

  if (name)
    strcpy(processes[pid].name, name);
  else
    strcpy(processes[pid].name, "");

  sigemptyset(&processes[pid].sigpending);
  sigemptyset(&processes[pid].sigdelivering);
    
  for (int i = 0; i < NB_FILES_MAX; ++i) {

    processes[pid].fds[i].fd = -1;
  }

  processes[pid].status = 0;
  processes[pid].wait_child = 0;
  processes[pid].wait_pid = 0;
  processes[pid].wait_options = 0;

  processes[pid].peer_addr.sun_family = AF_UNIX;
  sprintf(processes[pid].peer_addr.sun_path, "channel.process.%d", pid);

  if (ppid > 0) {

    for (int i = 0; i < (NB_FILES_MAX/8+1); ++i)
      processes[pid].fd_map[i] = processes[ppid].fd_map[i];
    
    for (int i = 0; i < NB_FILES_MAX; ++i) {

      if (processes[ppid].fds[i].fd >= 0) {
	
	processes[pid].fds[i].fd = processes[ppid].fds[i].fd;
	processes[pid].fds[i].remote_fd = processes[ppid].fds[i].remote_fd;
	processes[pid].fds[i].type = processes[ppid].fds[i].type;
	processes[pid].fds[i].major = processes[ppid].fds[i].major;
	processes[pid].fds[i].minor = processes[ppid].fds[i].minor;
	processes[pid].fds[i].fd_flags = processes[ppid].fds[i].fd_flags;
	processes[pid].fds[i].fs_flags = processes[ppid].fds[i].fs_flags;
	//strcpy(processes[pid].fds[i].peer, processes[ppid].fds[i].peer);
      }
    }
  }
  else {

    for (int i = 0; i < (NB_FILES_MAX/8+1); ++i)
      processes[pid].fd_map[i] = 0;
  }

  processes[pid].timerfd = -1;
  
  ++nb_processes;

  return pid;
}

void process_reset_sigactions(pid_t pid) {

  sigemptyset(&processes[pid].sigprocmask);

  memset(&processes[pid].sigactions, 0, 32*sizeof(struct sigaction));
}

int process_get_state(pid_t pid) {

  pid = pid & 0xffff;

  return processes[pid].proc_state;
}

void dump_processes() {

  emscripten_log(EM_LOG_CONSOLE,"**** processes ****");

  for (int i = 0; i < nb_processes; ++i) {

    emscripten_log(EM_LOG_CONSOLE, "* %d %d %d %d %s %d", processes[i].pid, processes[i].ppid, processes[i].pgid, processes[i].sid, processes[i].name, processes[i].proc_state);
  }
}

int process_find_smallest_fd(pid_t pid) {

  int i, j;

  pid = pid & 0xffff;

  for (i = 0; i < NB_FILES_MAX; ++i) {

    if ((processes[pid].fd_map[i/8] & 1 << (i%8)) == 0)
      return i;
  }

  return -1;
}

int process_create_fd(pid_t pid, int remote_fd, unsigned char type, unsigned short major, unsigned short minor, int flags) {
  
  int i;

  pid = pid & 0xffff;
  
  for (i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == -1)
      break;
  }

  if (i >= NB_FILES_MAX)
    return -1;

  int fd = process_find_smallest_fd(pid);

  processes[pid].fd_map[fd/8] |= (1 << (fd%8));

  processes[pid].fds[i].fd = fd;
  processes[pid].fds[i].remote_fd = remote_fd;
  processes[pid].fds[i].type = type;
  processes[pid].fds[i].major = major;
  processes[pid].fds[i].minor = minor;
  processes[pid].fds[i].fs_flags = flags;
  processes[pid].fds[i].fd_flags = (flags & O_CLOEXEC)?FD_CLOEXEC:0;

  emscripten_log(EM_LOG_CONSOLE,"process_create_fd: %d, %d, %d", pid, remote_fd, fd);

  return fd;
}

int process_get_fd(pid_t pid, int fd, unsigned char * type, unsigned short * major, int * remote_fd) {

  pid = pid & 0xffff;

  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {
      
      *type = processes[pid].fds[i].type;
      *major = processes[pid].fds[i].major;
      *remote_fd = processes[pid].fds[i].remote_fd;

      emscripten_log(EM_LOG_CONSOLE,"process_get_fd: %d, %d, %d (%d), (%d;%d)", pid, *remote_fd, fd, i, *type, *major);

      return 0;
    }
  }

  emscripten_log(EM_LOG_CONSOLE,"process_get_fd: %d, %d not found", pid, fd);

  return -1;
}

int process_close_fd(pid_t pid, int fd) {

  pid = pid & 0xffff;

  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      processes[pid].fds[i].fd = -1;
      
      processes[pid].fd_map[fd/8] &= ~(1 << (fd%8));

      emscripten_log(EM_LOG_CONSOLE,"process_close_fd: %d, %d (%i)", pid, fd, i);
      
      return 0;
    }
  }

  emscripten_log(EM_LOG_CONSOLE,"process_close_fd: %d, %d not found", pid, fd);

  return -1;
}

int process_find_open_fd(unsigned char type, unsigned short major, int remote_fd) {

  for (int j = 0; j < nb_processes; ++j) {

    for (int i = 0; i < NB_FILES_MAX; ++i) {

      if (processes[j].fds[i].fd != -1) {

	if ( (processes[j].fds[i].type == type) && (processes[j].fds[i].major == major) && (processes[j].fds[i].remote_fd == remote_fd) ) {

	  //emscripten_log(EM_LOG_CONSOLE,"process_find_open_fd: %d, %d, %d", j, j, remote_fd);
	  return 1;
	  
	}
      }
    }
  }

  return -1;
}

int process_clone_fd(pid_t pid, int fd, pid_t pid_dest) {

  emscripten_log(EM_LOG_CONSOLE,"process_clone_fd: %d %d %d", pid, fd, pid_dest);

  pid = pid & 0xffff;

  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      pid_dest = pid_dest & 0xffff;

      int j;
  
      for (j = 0; j < NB_FILES_MAX; ++j) {

	if (processes[pid_dest].fds[j].fd == -1)
	  break;
      }

      if (j >= NB_FILES_MAX)
	return -1;

      int new_fd = process_find_smallest_fd(pid_dest);

      emscripten_log(EM_LOG_CONSOLE,"process_clone_fd: new fd = %d", new_fd);

      processes[pid_dest].fd_map[new_fd/8] |= (1 << (new_fd%8));

      processes[pid_dest].fds[j].fd = new_fd;
      processes[pid_dest].fds[j].remote_fd = processes[pid].fds[i].remote_fd;
      processes[pid_dest].fds[j].type = processes[pid].fds[i].type;
      processes[pid_dest].fds[j].major = processes[pid].fds[i].major;
      processes[pid_dest].fds[j].minor = processes[pid].fds[i].minor;
      processes[pid_dest].fds[j].fs_flags = processes[pid].fds[i].fs_flags;
      processes[pid_dest].fds[j].fd_flags = processes[pid].fds[i].fd_flags;

      emscripten_log(EM_LOG_CONSOLE,"process_clone_fd: clone done for pid %d -> %d", pid_dest, new_fd);
      
      return new_fd;
    }
  }

  emscripten_log(EM_LOG_CONSOLE,"process_clone_fd: %d, %d not found", pid, fd);

  return -1;
}

int process_set_fd_flags(pid_t pid, int fd, int flags) {

  pid = pid & 0xffff;
  
  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      processes[pid].fds[i].fd_flags = flags;
      
      return processes[pid].fds[i].fd_flags;
    }
  }

  return -1;
}

int process_get_fd_flags(pid_t pid, int fd) {

  pid = pid & 0xffff;
  
  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      return processes[pid].fds[i].fd_flags;
    }
  }

  return -1;
}

int process_set_fs_flags(pid_t pid, int fd, int flags) {

  int i;

  pid = pid & 0xffff;

  for (i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      processes[pid].fds[i].fs_flags &= ~(O_APPEND | O_ASYNC | O_DIRECT | O_NOATIME | O_NONBLOCK) | flags;

      break;

      // TODO change all fd of all process with same remote_fd
      
      
    }
  }

  //TODO: very inefficient, to be improved
  if (i < NB_FILES_MAX) {

    for (int j= 0; j < NB_PROCESSES_MAX; ++j) {

      if ( (processes[j].pid >= 0) && (processes[j].proc_state < ZOMBIE_STATE) ) {
	
	for (int k = 0; k < NB_FILES_MAX; ++k) {

	  if (processes[j].fds[k].fd >= 0) {
	    
	    if ( (processes[j].fds[k].remote_fd == processes[pid].fds[i].remote_fd) &&
		 (processes[j].fds[k].type == processes[pid].fds[i].type) &&
		 (processes[j].fds[k].minor == processes[pid].fds[i].minor) &&
		 (processes[j].fds[k].major == processes[pid].fds[i].major) )
	      processes[j].fds[k].fs_flags = processes[pid].fds[i].fs_flags;
	  }
	}
      }
    }

    return processes[pid].fds[i].fs_flags;
  }

  return -1;
}

int process_get_fs_flags(pid_t pid, int fd) {

  pid = pid & 0xffff;
  
  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd) {

      return processes[pid].fds[i].fs_flags;
    }
  }

  return -1;
}

void process_get_peer_addr(pid_t pid, struct sockaddr_un * addr) {

  /*pid = pid & 0xffff;
  
    return &processes[pid].peer_addr;*/

  addr->sun_family = AF_UNIX;
  sprintf(addr->sun_path, "channel.process.%d", pid);
}

pid_t process_group_exists(pid_t pgid) {

  for (int i = 0; i < nb_processes; ++i) {

    if (processes[i].pgid == pgid)
      return i;
  }

  return 0;
}

pid_t process_setsid(pid_t pid) {

  pid = pid & 0xffff;
  
  if (!process_group_exists(pid)) { // process is not process group leader

    emscripten_log(EM_LOG_CONSOLE,"process_setsid: successful -> %d", pid);
     
    processes[pid].pgid = pid;
    processes[pid].sid = pid;

    // TODO inform tty driver
    
    return pid;
  }

  return -1;
}

pid_t process_getsid(pid_t pid) {

  pid = pid & 0xffff;
  
  return processes[pid].sid;
}

pid_t process_getppid(pid_t pid) {

  pid = pid & 0xffff;
  
  return processes[pid].ppid;
}

pid_t process_getpgid(pid_t pid) {

  pid = pid & 0xffff;
  
  return processes[pid].pgid;
}

int process_setpgid(pid_t pid, pid_t pgid) {

  pid = pid & 0xffff;
  
  if (pgid == 0) {

    processes[pid].pgid = pid;
    return 0;
  }

  pid_t i = process_group_exists(pgid);

  if (i) {

    if (processes[pid].sid != processes[i].sid) // shall be in same session
      return -1;
  }

  processes[pid].pgid = pgid;

  return 0;
}

int process_dup(pid_t pid, int fd, int new_fd) {

  int i;

  pid = pid & 0xffff;
  
  for (i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd == fd)
      break;
  }

  if (i >= NB_FILES_MAX)
    return -1;

  if (new_fd >= 0) {

    if (new_fd == fd) {

      return new_fd;
    }
    else {

      process_close_fd(pid, new_fd);

      // TODO inform tty driver
    }
  }
  else {
    
    new_fd = process_find_smallest_fd(pid);
  }
  
  int j;
  
  for (j = 0; j < NB_FILES_MAX; ++j) {

    if (processes[pid].fds[j].fd == -1)
      break;
  }

  if (j >= NB_FILES_MAX)
    return -1;

  processes[pid].fds[j].fd = new_fd;
  processes[pid].fds[j].remote_fd = processes[pid].fds[i].remote_fd;
  processes[pid].fds[j].type = processes[pid].fds[i].type;
  processes[pid].fds[j].major = processes[pid].fds[i].major;
  processes[pid].fds[j].minor = processes[pid].fds[i].minor;
  processes[pid].fds[j].fd_flags = processes[pid].fds[i].fd_flags & ~FD_CLOEXEC; // deactivate FD_CLOEXEC for the copy
  processes[pid].fds[j].fs_flags = processes[pid].fds[i].fs_flags;

  processes[pid].fd_map[new_fd/8] |= (1 << (new_fd%8));
  
  return new_fd;
}

char * process_getcwd(pid_t pid) {

  pid = pid & 0xffff;
  
  return processes[pid].cwd;
}

int process_chdir(pid_t pid, char * dir) {

  pid = pid & 0xffff;
  
  if (strlen(dir) < (1024-1))
    strcpy(processes[pid].cwd, dir);

  return 0;
}

EM_JS(void, exit_proc, (int pid), {

    pid = pid & 0xffff;
    
    let m = {
	    
      type: 4,   // exit
      pid: pid
    };

    window.parent.postMessage(m);
});

void process_terminate(pid_t pid) {

  pid = pid & 0xffff;
  
  processes[pid].proc_state = EXITED_STATE;
  processes[pid].pid = -1;

  //TOTEST
  exit_proc(pid);

  for (int i = 0; i < nb_processes; ++i) {

    if (processes[i].ppid == pid) {
      
      if (processes[i].proc_state == ZOMBIE_STATE) {

	process_terminate(i);
      }
      else if (processes[i].proc_state != EXITED_STATE) {

	emscripten_log(EM_LOG_CONSOLE, "process_wait: attach process %d to resmgr", i);

	processes[i].ppid = 1; // attach process to resmgr
      }
    
    }
  }
}

pid_t process_wait(pid_t ppid, pid_t pid, int options, int * status) {

  int ret = -1;
  
  if (pid > 0)
    pid = pid & 0xffff;
  
  ppid = ppid & 0xffff;
  
  for (int i = 0; i < nb_processes; ++i) {

    if ( (processes[i].ppid == ppid) && (processes[i].proc_state == ZOMBIE_STATE) ) {
      if ( (pid == -1) || // any process
	   ( (pid > 0) && (pid == i) ) ||  // exact pid
	   ( (pid == 0) && (processes[i].pgid == processes[ppid].pgid) ) ||  // sam group
	   ( (pid < -1) && (processes[i].pgid = -pid) ) ) {

	emscripten_log(EM_LOG_CONSOLE, "process_wait: found child pid %d", i);
	
	ret = i;
	break;
      }
    }
  }

  if (ret >= 0) { // found a zombie child

    *status = processes[ret].status;

    process_terminate(ret);
  }
  else if (!(options & WNOHANG)) { // wait for a child process in a blocking way

    processes[ppid].wait_child = 1;
    processes[ppid].wait_pid = pid;
    processes[ppid].wait_options = options;

    ret = 0;
  }

  return ret;
}

void process_to_zombie(pid_t pid, int status) {

  pid = pid & 0xffff;
  
  processes[pid].proc_state = ZOMBIE_STATE;
  processes[pid].status = status;

  del_proc_entry(pid);
}

pid_t process_exit(pid_t pid, int sock) {

  emscripten_log(EM_LOG_CONSOLE, "process_exit: pid=%d state=%d", pid, processes[pid].proc_state);
  
  if (processes[pid].proc_state != ZOMBIE_STATE)
    return 0;

  int ppid = processes[pid].ppid;

  emscripten_log(EM_LOG_CONSOLE, "process_exit: ppid=%d wait_child=%d wait_pid=%d", ppid, processes[ppid].wait_child, processes[ppid].wait_pid);
  
  //TODO: case of several pthread 
  
  if ( (ppid == 1) || (processes[ppid].wait_child &&
		       ( (processes[ppid].wait_pid == -1) || (processes[ppid].wait_pid == pid) ) ) ) {

    // TODO: add other conditions (group)

    emscripten_log(EM_LOG_CONSOLE, "process_exit: found parent pid %d", ppid);
    
    process_terminate(pid);

    processes[ppid].wait_child = 0;

    if (ppid > 1) { // parent (except resmgr) is waiting child's exit 

      char buf[256];
      struct message * msg = &buf[0];

      msg->msg_id = WAIT|0x80;
      msg->pid = ppid;
      msg->_errno = 0;

      msg->_u.wait_msg.pid = pid;
      msg->_u.wait_msg.status = processes[pid].status;

      emscripten_log(EM_LOG_CONSOLE, "finish_exit: Send wait response to parent %d -> status=%d", msg->pid, msg->_u.wait_msg.status);
    
      // Forward response to process

      struct sockaddr_un addr;

      process_get_peer_addr(msg->pid, &addr);
	
      sendto(sock, (char *)msg, 256, 0, (struct sockaddr *)&addr, sizeof(struct sockaddr_un));
    }
      
    return ppid;
  }
  
  return 0;
}

pid_t process_exit_child(pid_t ppid, int sock) {

  if (processes[ppid].wait_child) {

    for (int i=0; i < nb_processes; ++i) {

      if ( (processes[i].ppid == ppid) && (processes[i].proc_state == ZOMBIE_STATE) && ( (processes[ppid].wait_pid == -1) || (processes[ppid].wait_pid == i) ) ) {

	return process_exit(i, sock);
      }
    }
  }

  return 0;
}

int process_sigaction(pid_t pid, int signum, struct sigaction * act) {

  struct sigaction old;

  if (!signum || (signum > NSIG) )
    return -1;

  pid = pid & 0xffff;

  memcpy(&old, &processes[pid].sigactions[signum-1], sizeof(struct sigaction));

  memcpy(&processes[pid].sigactions[signum-1], act, sizeof(struct sigaction));

  memcpy(act, &old, sizeof(struct sigaction));

  return 0;
}

int process_sigprocmask(pid_t pid, int how, sigset_t * set) {

  sigset_t old;
  unsigned char * set2 = (unsigned char *)set;
  unsigned char * mask = (unsigned char *)&processes[pid].sigprocmask;

  pid = pid & 0xffff;

  memcpy(&old, &processes[pid].sigprocmask, sizeof(sigset_t));

  switch(how) {

  case SIG_BLOCK:

    for (int i = 0; i < NSIG; ++i) {

      if (set2[i/8] & (1 << (i%8))) {

	mask[i/8] |= 1 << (i%8);
      }
    }
    
    break;

  case SIG_UNBLOCK:

    for (int i = 0; i < NSIG; ++i) {

      if (set2[i/8] & (1 << (i%8))) {

	mask[i/8] &= ~(1 << (i%8));
      }
    }

    break;

  case SIG_SETMASK:

    memcpy(&processes[pid].sigprocmask, set, sizeof(sigset_t));
    break;

  default:
    
    break;
  }

  memcpy(set, &old, sizeof(sigset_t));
  
  return 0;
}

int process_kill(pid_t pid, int signum, struct sigaction * act, int sock) {

  pid = pid & 0xffff;

  if (processes[pid].proc_state != RUNNING_STATE)
    return 0;
  
  int action = 0; // No action
  
  if ( (signum == SIGKILL) || (signum == SIGSTOP) ) {

    sigaddset(&processes[pid].sigpending, signum);

    // Cannot change default behaviour for SIGKILL and SIGSTOP
      
    action = 1; // Default action
  }
  else if ( (((int)processes[pid].sigactions[signum-1].sa_handler) != -2) && (!sigismember(&processes[pid].sigdelivering, signum)) ) { // Signal not ignored

    sigaddset(&processes[pid].sigpending, signum);

    if (!sigismember(&processes[pid].sigprocmask, signum)) {
      
      memcpy(act, &processes[pid].sigactions[signum-1], sizeof(struct sigaction));

      if ( ((int)processes[pid].sigactions[signum-1].sa_handler) == 0) {
	
	action = 1; // Default action
      }
      else {

	sigaddset(&processes[pid].sigdelivering, signum);
	
	action = 2; // Custom action

	char buf[256];
	struct message * msg = &buf[0];

	msg->msg_id = KILL;
	msg->pid = 1;
	msg->_u.kill_msg.pid = pid;
	msg->_u.kill_msg.sig = signum;
	memcpy(&msg->_u.kill_msg.act, &processes[pid].sigactions[signum-1], sizeof(struct sigaction));
	
	struct sockaddr_un addr;
	
	process_get_peer_addr(pid, &addr);

	emscripten_log(EM_LOG_CONSOLE, "process_kill: send signal %d to pid %d (%s)", signum, pid, addr.sun_path);

	sendto(sock, buf, 256, 0, (struct sockaddr *) &addr, sizeof(addr));
      }
    }
  }

  emscripten_log(EM_LOG_CONSOLE, "<-- process_kill: pid=%d sig=%d action=%d", pid, signum, action);

  return action;
}

void process_signal_delivered(pid_t pid, int signum) {

  pid = pid & 0xffff;
  
  sigdelset(&processes[pid].sigdelivering, signum);
}

int process_setitimer(pid_t pid, int which, int val_sec, int val_usec, int it_sec, int it_usec) {

  pid = pid & 0xffff;
  
  if (processes[pid].timerfd < 0)
    processes[pid].timerfd = timerfd_create(CLOCK_MONOTONIC, 0);

  struct itimerspec ts;
     
  ts.it_interval.tv_sec = it_sec;
  ts.it_interval.tv_nsec = it_usec * 1000;
  ts.it_value.tv_sec = val_sec;
  ts.it_value.tv_nsec = val_usec * 1000;
  
  if ( (ts.it_value.tv_sec == 0) && (ts.it_value.tv_nsec == 0) ) {

    ts.it_value.tv_sec = it_sec;
    ts.it_value.tv_nsec = it_usec * 1000;
  }
     
  timerfd_settime(processes[pid].timerfd, 0, &ts, NULL);
  
  return processes[pid].timerfd;
}

void process_clearitimer(pid_t pid) {

  pid = pid & 0xffff;
  
  if (processes[pid].timerfd >= 0) {
    close(processes[pid].timerfd);
    processes[pid].timerfd = -1;
  }
}

int process_opened_fd(pid_t pid, unsigned char * type, unsigned short * major, int * remote_fd, int flag) {

  pid = pid & 0xffff;
  
  for (int i = 0; i < NB_FILES_MAX; ++i) {

    if (processes[pid].fds[i].fd >= 0) {

      if (!flag || (processes[pid].fds[i].fd_flags & flag)) {

	*type = processes[pid].fds[i].type;
	*major = processes[pid].fds[i].major;
	*remote_fd = processes[pid].fds[i].remote_fd;

	return processes[pid].fds[i].fd;
      }
    }
  }

  return -1;
}

int process_get_session(pid_t sid, pid_t session[], int size) {

  int i = 0;

  for (int j= 0; j < NB_PROCESSES_MAX; ++j) {

    if ( (processes[j].pid >= 0) && (processes[j].proc_state < ZOMBIE_STATE) ) {
	
      if (processes[j].sid == sid) {

	session[i++] = processes[j].pid;

	if (i == size)
	  return i;
      }
    }
  }

  return i;
}

int process_get_group(pid_t pgid, pid_t group[], int size) {

  int i = 0;

  for (int j= 0; j < NB_PROCESSES_MAX; ++j) {

    if ( (processes[j].pid >= 0) && (processes[j].proc_state < ZOMBIE_STATE) ) {
	
      if (processes[j].pgid == pgid) {

	group[i++] = processes[j].pid;

	if (i == size)
	  return i;
      }
    }
  }

  return i;
}
