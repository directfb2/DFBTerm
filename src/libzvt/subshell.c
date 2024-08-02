/*
 * Subprocess activation under a pseudo terminal support for zvtterm
 *
 * Copyright (C) 1994, 1995, 1998 Dugan Porter
 * Copyright (C) 1998 Michael Zucchi
 * Copyrithg (C) 1995, 1998 Miguel de Icaza
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of Version 2 of the GNU Library General Public
 * License, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>

#include <sys/types.h>

#include "subshell-includes.h"
#define ZVT_TERM_DO_UTMP_LOG 1
#define ZVT_TERM_DO_WTMP_LOG 2
#define ZVT_TERM_DO_LASTLOG  4

/* Pid of the helper SUID process */
static pid_t helper_pid;

/* Whether sigchld signal handler has been established yet */
static int sigchld_inited = 0;

/* Points to a possibly previously installed sigchld handler */
static struct sigaction old_sigchld_handler;

/* The socketpair used for the protocol */
int helper_socket_protocol  [2];

/* The paralell socketpair used to transfer file descriptors */
int helper_socket_fdpassing [2];

/* List of all subshells we're watching */
static GSList *children;

typedef struct {
	pid_t pid;
	int fd;
	int listen_fd;
	int exit_status;	/* waitpid return value */
	gboolean dead;		/* if the process is already dead  */
} child_info_t;

/*
 *  Fork the subshell, and set up many, many things.
 *
 */

static void
sigchld_handler (int signo)
{
	GSList *l;
	int status;

	if (waitpid (helper_pid, &status, WNOHANG) == helper_pid){
		helper_pid = 0;
		return;
	}

	for (l = children; l; l = l->next){
		child_info_t *child = l->data;
		
		if (waitpid (child->pid, &status, WNOHANG) == child->pid){
			child->exit_status = status;
			child->dead = 1;
			(void)!write (child->fd, "D", 1);
			return;
		}
	}
	/* No children of ours, chain */
	if (old_sigchld_handler.sa_handler)
		(*old_sigchld_handler.sa_handler)(signo);
}

#ifdef HAVE_SENDMSG
#include <sys/socket.h>
#include <sys/uio.h>

#ifdef HAVE_SYS_UN_H /* Linux libc5 */
#include <sys/un.h>
#endif

#ifndef CMSG_DATA /* Linux libc5 */
/* Ancillary data object manipulation macros.  */
#if !defined __STRICT_ANSI__ && defined __GNUC__ && __GNUC__ >= 2
# define CMSG_DATA(cmsg) ((cmsg)->cmsg_data)
#else
# define CMSG_DATA(cmsg) ((unsigned char *) ((struct cmsghdr *) (cmsg) + 1))
#endif
#endif /* CMSG_DATA */

static struct cmsghdr *cmptr;
#define CONTROLLEN  sizeof (struct cmsghdr) + sizeof (int)

static int
receive_fd (int helper_fd)
{
	struct iovec iov [1];
	struct msghdr msg;
	char buf [32];
	
	iov [0].iov_base = buf;
	iov [0].iov_len  = sizeof (buf);
	msg.msg_iov      = iov;
	msg.msg_iovlen   = 1;
	msg.msg_name     = NULL;
	msg.msg_namelen  = 0;

	if (cmptr == NULL && (cmptr = g_malloc (CONTROLLEN)) == NULL)
		return -1;
	msg.msg_control = (caddr_t) cmptr;
	msg.msg_controllen = CONTROLLEN;

	if (recvmsg (helper_fd, &msg, 0) <= 0)
		return -1;

	int *ret_data = (int *) CMSG_DATA (cmptr);

	return *ret_data;
}

static int
s_pipe (int fd [2])
{
	return socketpair (AF_UNIX, SOCK_STREAM, 0, fd);
}

#elif defined(__sgi) && !defined(HAVE_SENDMSG)

/* 
 * IRIX 6.2 is like 4.3BSD; it will not have HAVE_SENDMSG set, 
 * because msghdr used msg_accrights and msg_accrightslen rather 
 * than the newer msg_control and msg_controllen fields configure
 * checks.  The SVR4 code below doesn't work because pipe()
 * semantics are controlled by the svr3pipe systune variable, 
 * which defaults to uni-directional pipes.  Also sending
 * file descriptors through pipes isn't implemented.
 */

#include <sys/socket.h>
#include <sys/uio.h>

static int
receive_fd (int helper_fd)
{
	struct iovec iov [1];
	struct msghdr msg;
	char buf [32];
	int fd;

	iov [0].iov_base = buf;
	iov [0].iov_len  = sizeof (buf);
	msg.msg_iov      = iov;
	msg.msg_iovlen   = 1;
	msg.msg_name     = NULL;
	msg.msg_namelen  = 0;

	msg.msg_accrights = (caddr_t) &fd;
	msg.msg_accrightslen = sizeof(fd);

	if (recvmsg (helper_fd, &msg, 0) <= 0)
		return -1;

	return fd;
}

static int
s_pipe (int fd [2])
{
	return socketpair (AF_UNIX, SOCK_STREAM, 0, fd);
}

#else

static int
receive_fd (int helper_fd)
{
	struct strrecvfd recvfd;
	
	if (ioctl (helper_fd, I_RECVFD, &recvfd) < 0)
		return -1;

	return recvfd.fd;
}

static int
s_pipe (int fd [2])
{
	return pipe (fd);
}
#endif

static void *
get_ptys (int *master, int *slave, int update_wutmp)
{
	GnomePtyOps op;
	int result, n;
	void *tag;
	
	if (helper_pid == -1)
		return NULL;

	if (helper_pid == 0){
		if (s_pipe (helper_socket_protocol) == -1)
			return NULL;

		if (s_pipe (helper_socket_fdpassing) == -1){
			close (helper_socket_protocol [0]);
			close (helper_socket_protocol [1]);
			return NULL;
		}
		
		helper_pid = fork ();
		
		if (helper_pid == -1){
			close (helper_socket_protocol [0]);
			close (helper_socket_protocol [1]);
			close (helper_socket_fdpassing [0]);
			close (helper_socket_fdpassing [1]);
			return NULL;
		}

		if (helper_pid == 0){
			close (0);
			close (1);
			dup2 (helper_socket_protocol  [1], 0);
			dup2 (helper_socket_fdpassing [1], 1);

			/* Close aliases */
			close (helper_socket_protocol  [0]);
			close (helper_socket_protocol  [1]);
			close (helper_socket_fdpassing [0]);
			close (helper_socket_fdpassing [1]);

			execl (PTY_HELPER_DIR "/gnome-pty-helper", "gnome-pty-helper", NULL);
			exit (1);
		} else {
			close (helper_socket_fdpassing [1]);
			close (helper_socket_protocol  [1]);

			/*
			 * Set the close-on-exec flag for the other
			 * descriptors, these should never propagate
			 * (otherwise gnome-pty-heler wont notice when
			 * this process is killed).
			 */
			fcntl (helper_socket_protocol [0], F_SETFD, FD_CLOEXEC);
			fcntl (helper_socket_fdpassing [0], F_SETFD, FD_CLOEXEC);
		}
	}
	op = GNOME_PTY_OPEN_NO_DB_UPDATE;
	
	if (update_wutmp & ZVT_TERM_DO_UTMP_LOG){
		if (update_wutmp & (ZVT_TERM_DO_WTMP_LOG | ZVT_TERM_DO_LASTLOG))
			op = GNOME_PTY_OPEN_PTY_LASTLOGUWTMP;
		else if (update_wutmp & ZVT_TERM_DO_WTMP_LOG)
			op = GNOME_PTY_OPEN_PTY_UWTMP;
		else if (update_wutmp & ZVT_TERM_DO_LASTLOG)
			op = GNOME_PTY_OPEN_PTY_LASTLOGUTMP;
		else
			op = GNOME_PTY_OPEN_PTY_UTMP;
	} else if (update_wutmp & ZVT_TERM_DO_WTMP_LOG) {
		if (update_wutmp & (ZVT_TERM_DO_WTMP_LOG | ZVT_TERM_DO_LASTLOG))
			op = GNOME_PTY_OPEN_PTY_LASTLOGWTMP;
		else if (update_wutmp & ZVT_TERM_DO_WTMP_LOG)
			op = GNOME_PTY_OPEN_PTY_WTMP;
	} else
		if (update_wutmp & ZVT_TERM_DO_LASTLOG)
			op = GNOME_PTY_OPEN_PTY_LASTLOG;
	
	if (write (helper_socket_protocol [0], &op, sizeof (op)) < 0)
		return NULL;
	
	n = n_read (helper_socket_protocol [0], &result, sizeof (result));
	if (n == -1 || n != sizeof (result)){
		helper_pid = 0;
		return NULL;
	}
	
	if (result == 0)
		return NULL;

	n = n_read (helper_socket_protocol [0], &tag, sizeof (tag));
	
	if (n == -1 || n != sizeof (tag)){
		helper_pid = 0;
		return NULL;
	}

	*master = receive_fd (helper_socket_fdpassing [0]);
	*slave  = receive_fd (helper_socket_fdpassing [0]);
	
	return tag;
}

/**
 * zvt_init_subshell:
 * @vt:       the terminal emulator object.
 * @pty_name: Name of the pseudo terminal opened.
 * @log:      if TRUE, then utmp/wtmp records are updated
 *
 * Returns the pid of the subprocess.
 */
int
zvt_init_subshell (struct vt_em *vt, char *pty_name, int log)
{
	int slave_pty, master_pty;
	struct sigaction sa;
	child_info_t *child;
	pid_t pid;
	int status;
	int p[2];

	g_return_val_if_fail (vt != NULL, -1);

	if (!sigchld_inited){
		sigset_t sigset;

		sigemptyset(&sigset);
		sigaddset(&sigset, SIGPIPE);
		sigaddset(&sigset, SIGCHLD);
		sigprocmask(SIG_UNBLOCK, &sigset, NULL);

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = sigchld_handler;
		sigaction (SIGCHLD, &sa, &old_sigchld_handler);
		sigchld_inited = 1;
	}

	if ((vt->pty_tag = get_ptys (&master_pty, &slave_pty, log)) == NULL)
		return -1;

	/* Fork the subshell */

	vt->childpid = fork ();
	
	if (vt->childpid == -1)
		return -1;

	if (vt->childpid == 0)  /* We are in the child process */
	{
		close (master_pty);
		login_tty (slave_pty);

		/*
		 * Reset the child signal handlers
		 */
		signal (SIGINT,  SIG_DFL);
		signal (SIGQUIT, SIG_DFL);
		signal (SIGCHLD, SIG_DFL);
		signal (SIGPIPE, SIG_DFL);
		
		/*
		 * These should be turned off.  Login does turn them off
		 * If the user shells supports these, they will be turned
		 * back on
		 */
		signal (SIGTSTP, SIG_IGN);
		signal (SIGTTIN, SIG_IGN);
		signal (SIGTTOU, SIG_IGN);
	} else {
		close (slave_pty);

		(void)!pipe(p);

		vt->msgfd = p [0];
		
		child = g_new (child_info_t, 1);
		child->pid = vt->childpid;
		child->fd = p[1];
		child->listen_fd = p [0];
		child->dead = 0;
		child->exit_status = 0;
		children = g_slist_prepend (children, child);
		
		/* We could have received the SIGCHLD signal for the subshell 
		 * before installing the init_sigchld
		 */
		pid = waitpid (vt->childpid, &status, WUNTRACED | WNOHANG);
		if (pid == vt->childpid && child->pid >= 0){
			child->pid = 0;
			(void)!write (child->fd, "D", 1);
			return -1;
		}
		
		vt->keyfd = vt->childfd = master_pty;

	}
	
	return vt->childpid;
}

int zvt_resize_subshell (int fd, int col, int row, int xpixel, int ypixel)
{
#if defined TIOCSWINSZ && !defined SCO_FLAVOR
    struct winsize tty_size;

    tty_size.ws_row = row;
    tty_size.ws_col = col;
    tty_size.ws_xpixel = xpixel;
    tty_size.ws_ypixel = ypixel;

    return (ioctl (fd, TIOCSWINSZ, &tty_size));
#endif
}

/**
 * zvt_shutdown_subsheel:
 * @vt: The terminal emulator object
 * 
 * Shuts down the subshell process.
 *
 * Return value: The exit status of the process, or -1 on error.
 **/
int
zvt_shutdown_subshell (struct vt_em *vt)
{
	GnomePtyOps op;
	GSList *l;

	g_return_val_if_fail (vt != NULL, -1);

	/* shutdown pty through helper */
	if (vt->pty_tag) {
		op = GNOME_PTY_CLOSE_PTY;
		(void)!write (helper_socket_protocol [0], &op, sizeof (op));
		(void)!write (helper_socket_protocol [0], &vt->pty_tag, sizeof (vt->pty_tag));
		vt->pty_tag = NULL;
	}

	/* close the child comms link(s) */
	close(vt->childfd);
	if (vt->keyfd != vt->childfd)
		close(vt->keyfd);
	vt->msgfd = vt->childfd = -1;

	/* remove the child node */
	for (l = children; l; l = l->next){
		child_info_t *child = l->data;

		if (child->pid == vt->childpid){
			int status;
			
			if (!child->dead){
				/* make sure the child has quit! */
				kill (vt->childpid, SIGHUP);
				waitpid (vt->childpid, &child->exit_status, 0);
			}
			
			status = child->exit_status;
			close (child->fd);
			g_free (child);
			children = g_slist_remove (children, l->data);

			return WEXITSTATUS(status);
		}
	}

	/* unknown child? */
	return -1;
}
