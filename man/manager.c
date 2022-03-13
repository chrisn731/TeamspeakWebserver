/*
 * Teamspeak Manager
 *
 * The purpose of this is to manage and control the teamspeak bot
 * and webserver. It is able to automatically stop, restart, and
 * recover a crashed process.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "client.h"
#include "manager.h"
#include "session.h"

#define __noreturn __attribute__((__noreturn__))

#define LOG_FILE_PATH "/tmp/ts_manager_log.txt"
#define WEBSERVER_PATH "./tswebserver"
#define BOT_PATH "./bot.py"

#define LOG_BUF_SIZE (1 << 13)
#define MAN_POLL_TIMEOUT -1 /* In milliseconds, -1 for no tiemout */

#define intr_enable()							\
	do {								\
		sigset_t __enable;					\
		sigfillset(&__enable);					\
		sigprocmask(SIG_UNBLOCK, &__enable, NULL);		\
	} while (0)

#define intr_save(flags) 				\
	do {						\
		sigset_t __s;				\
		sigemptyset(&__s);			\
		sigemptyset(&flags);			\
		sigaddset(&__s, SIGCHLD);		\
		sigaddset(&__s, SIGTERM);		\
		sigprocmask(SIG_BLOCK, &__s, &flags);	\
	} while (0)

#define intr_restore(flags) 						\
	do {								\
		sigprocmask(SIG_SETMASK, &flags, NULL);			\
	} while (0)


#define MODULE_RUNNING	0x0000
#define MODULE_OFF 	0x0001
#define MODULE_DEAD	0x0002
#define MODULE_EXITED	0x0004

#define MODULE_INACTIVE (MODULE_OFF | MODULE_DEAD | MODULE_EXITED)
#define MODULE_PARKED (MODULE_OFF | MODULE_EXITED)
#define module_is_running(m) ((m)->state == MODULE_RUNNING)
#define module_is_inactive(m) ((m)->state & MODULE_INACTIVE)
#define module_is_parked(m) ((m)->state & MODULE_PARKED)

#define LOG_ERRNO	0x01
#define LOG_INFO 	0x02
#define LOG_ERR		0x04
#define LOG_FATAL	0x08
#define die(s, ...) do_log(LOG_FATAL, "[FATAL] " s, ## __VA_ARGS__)
#define diev(s, ...) do_log(LOG_FATAL | LOG_ERRNO, "[FATAL] " s, ## __VA_ARGS__)
#define log_info(s, ...) do_log(LOG_INFO, "[INFO] " s, ## __VA_ARGS__)
#define log_err(s, ...) do_log(LOG_ERR, "[ERR] " s, ## __VA_ARGS__)
#define logv_err(s, ...) do_log(LOG_ERR | LOG_ERRNO, "[ERR] " s, ## __VA_ARGS__)

struct module {
	/* pathname & argv - args to be used for exec() calls */
	const char *mod_name;
	const char *pathname;
	char *const argv[5];
	void (*init)(void);
	pid_t pid;

	/*
	 * num_*_fails - keeps track of how many times a module has failed
	 * If the number of fails for a module is >= MAX_FAIL_FOR_STOP the
	 * module is NOT reloaded if it is terminated.
	 */
#define MAX_FAIL_FOR_STOP 5
	unsigned int num_fails;
	unsigned int needs_restart;
	unsigned int state;
	int wstatus;
};
#define DEFINE_MODULE(name, init_func, path, ...)		\
	struct module name = {					\
		.mod_name = #name,				\
		.pathname = path,				\
		.argv = { __VA_ARGS__, NULL},			\
		.pid = -1,					\
		.init = init_func,				\
		.state = MODULE_OFF				\
	}

static struct manager manager;

static void init_ts_bot(void);
static void init_ts_webserver(void);
static DEFINE_MODULE(ts_bot, &init_ts_bot, "/usr/bin/python", "python", BOT_PATH);
static DEFINE_MODULE(ts_webserver, &init_ts_webserver, WEBSERVER_PATH, WEBSERVER_PATH);

static struct module *mods[NUM_MODS] = {
	&ts_bot,
	&ts_webserver,
};

static char __log_buf[LOG_BUF_SIZE];

static void usage(const char *self)
{
	fprintf(stdout,
		"Usage: %s [-s {command} | [-a] [-w] [-b]]\n"
		"  -s    Send a command to the currently running manager\n"
		"  -a    Start the manager with all modules\n"
		"  -b    Start the manager with only the bot\n"
		"  -w    Start the manager with only the webserver\n"
		"\n"
		"Examples:\n"
		"  %s -a (Start up the manager)\n"
		"  %s -s stop (Send the stop command)\n",
		self, self, self);
	exit(1);
}

/*
 * do_log - Print log string
 *
 * In this function we print to stdout, but the manager uses the
 * stdout file descriptor as a log file
 */
static void do_log(int log_flags, const char *fmt, ...)
{
	va_list argp;
	size_t nw;
	char *buffer = __log_buf;
	unsigned int flags = log_flags;
	int errv = errno;

	va_start(argp, fmt);
	nw = vsnprintf(buffer, LOG_BUF_SIZE, fmt, argp);
	va_end(argp);
	if (nw < 0 || nw >= LOG_BUF_SIZE) {
		nw = sizeof(buffer) - 1;
		goto log_fail;
	}

	if (flags & LOG_ERRNO)
		nw += snprintf(buffer + nw, LOG_BUF_SIZE - nw, " - %s",
							strerror(errv));
log_fail:
	buffer[nw] = '\n';
	write(STDOUT_FILENO, buffer, nw + 1);
	if (flags & LOG_FATAL)
		exit(1);
}

/*
 * init_ts_bot - Teamspeak bot startup function
 */
static void __noreturn init_ts_bot(void)
{
	execv(ts_bot.pathname, ts_bot.argv);
	logv_err("[BOT ERR] - failed to startup bot");
	_exit(1);
}

/*
 * init_ts_webserver - Teamspeak webserver startup function
 */
static void __noreturn init_ts_webserver(void)
{
	if (chdir("./webserver") < 0)
		logv_err("Could not change into webserver directory.");
	else
		execv(ts_webserver.pathname, ts_webserver.argv);
	logv_err("[SERVER ERR] - failed to startup server");
	_exit(1);
}

/*
 * Remove all signal handlers. This is mainly only used by new child proceeses.
 */
static void teardown_sighands(void)
{
	struct sigaction del = {
		.sa_handler = SIG_DFL,
	};
	if (sigaction(SIGTERM, &del, NULL) < 0 ||
	    sigaction(SIGCHLD, &del, NULL) < 0)
		logv_err("Failed to teardown signal handlers!");

}

/*
 * do_module_init - fork and attempt to initialize module
 *
 * This function calls prctl() (which is linux specific BTW) so that our
 * initialized module dies off if the manager were to unexpectedly die.
 *
 * This function also sets up a communication pipe between the parent and
 * it's children. This is so we can nicely write down what they are saying
 * in a better format than having them write it themselves.
 */
static int do_module_init(struct module *mod)
{
	sigset_t flags;
	int cpid, i, pipefds[2];

	log_info("Attempting to start up '%s'", mod->mod_name);
	if (module_is_running(mod)) {
		log_err("%s is already running!", mod->mod_name);
		goto bad_init;
	}
	if (pipe(pipefds) < 0) {
		logv_err("Failed to set up pipe for %s", mod->mod_name);
		goto bad_init;
	}

	for (i = 0; i < NUM_MODS; i++) {
		if (!manager.mod_pipes[i].pipefd) {
			manager.mod_pipes[i].pipefd = pipefds[0];
			manager.mod_pipes[i].mod_name = mod->mod_name;
			break;
		}
	}
	if (i >= NUM_MODS) {
		log_err("%s: Manager has no open pipes for '%s' module!",
				__func__, mod->mod_name);
		goto bad_init_cleanup_pipe;
	}

	intr_save(flags);
	cpid = fork();
	switch (cpid) {
	case -1:
		logv_err("Failed to fork for %s", mod->mod_name);
		goto bad_init_cleanup_fork;
	case 0:
		/* We don't need that read end */
		if (close(pipefds[0]) < 0) {
			logv_err("%s: failed to close read end of pipe for '%s'",
					__func__, mod->mod_name);
		}
		/* Have the output of our module point to the write end */
		if (dup2(pipefds[1], STDOUT_FILENO) < 0 ||
		    dup2(pipefds[1], STDERR_FILENO) < 0) {
			logv_err("%s: '%s' failed to dup",
					__func__, mod->mod_name);
			_exit(1);
		}
		/* This fd is now useless */
		close(pipefds[1]);
		if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0) {
			logv_err("%s: '%s' failed to do prctl()\n",
					__func__, mod->mod_name);
			_exit(1);
		}
		if (!mod->init) {
			log_err("%s has no init function!!", mod->mod_name);
			_exit(1);
		}
		teardown_sighands();
		/* Completely restore all interrupts */
		intr_enable();
		mod->init(); /* DOES NOT RETURN */
		die("%s init function should not return", mod->mod_name);
		break;
	default:
		break;
	}
	/* From here on is the manager */
	mod->state = MODULE_RUNNING;
	mod->pid = cpid;
	intr_restore(flags);
	if (close(pipefds[1]) < 0)
		logv_err("Failed to close write end of pipe for '%s'",
						mod->mod_name);

	/*
	 * The parent should NOT block on reads from a pipe. Manager needs to
	 * stay on his toes.
	 */
	if (fcntl(pipefds[0], F_SETFL, O_NONBLOCK) < 0)
		logv_err("Setting O_NONBLOCK on read end of pipe for '%s' failed!"
			" Manager will for SURE not work as expected!",
			mod->mod_name);
	manager.mods_dirty = 1;
	return 0;

bad_init_cleanup_fork:
	intr_restore(flags);
bad_init_cleanup_pipe:
	for (i = 0; i < NUM_MODS; i++) {
		if (!strcmp(manager.mod_pipes[i].mod_name, mod->mod_name)) {
			manager.mod_pipes[i].pipefd = pipefds[0];
			manager.mod_pipes[i].mod_name = "";
			break;
		}
	}
	for (i = 0; i < 2; i++)
		close(pipefds[i]);
bad_init:
	return -1;
}

/*
 * child_death_handler - cleanup after any dead modules
 *
 * Call on wait() to clean up after any modules that died. Then, mark them
 * as needing a restart so the manager can try to get them back on their feet.
 */
static void child_death_handler(int signum)
{
	pid_t dead_child;
	int status = 0;

	/* wait() on all dead children */
	while ((dead_child = waitpid(-1, &status, WNOHANG)) > 0) {
		int i;
		for (i = 0; i < ARRAY_SIZE(mods); i++) {
			struct module *mod = mods[i];
			if (dead_child == mod->pid && module_is_running(mod)) {
				mod->state = MODULE_DEAD;
				mod->needs_restart = 1;
				mod->pid = -1;
				mod->wstatus = status;
				manager.mods_dirty = 1;
			}
		}
	}
	if (dead_child < 0)
		logv_err("Failed to wait for stopped child.");
}

/*
 * We want all our output to be redirected to a log file so that when
 * we daemonize we can still see errors and such if something was to
 * go wrong.
 */
static void init_manager_log_file(void)
{
	int fd;

	fd = open(LOG_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		diev("Error opening/creating manager log file");
	if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
		diev("dup2: Error duping");
	close(fd);
}

/*
 * Setup a local Unix socket for IPC.
 * Used so that we can just recall this program with the '-s/-S' flag
 * and send a command so we can dynamically interact with the loaded
 * and running modules without having to completely kill and restart
 * the manager.
 */
static int setup_comm_socket(void)
{
	struct sockaddr_un sa;
	int sfd, flags, on = 1;

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		diev("Error creating socket file descriptor");
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		diev("Error setting SO_REUSEADDR failed for setsockopt");

	flags = fcntl(sfd, F_GETFD);
	if (flags < 0)
		diev("Error getting socket flags");
	if (fcntl(sfd, F_SETFD, flags | FD_CLOEXEC) < 0)
		diev("Error setting socket to close on exec");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (bind(sfd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		diev("Error while attempting to bind");
	if (listen(sfd, 1) < 0)
		diev("Error setting up socket to listen.");
	return sfd;
}

/*
 * init_manager - initalize log file, daemonzie, set up manager running state
 */
static void init_manager(void)
{
	struct stat st;
	time_t t;
	int nullfd, i;

	manager.status = STARTING;
	time(&t);
	manager.startup_time = localtime(&t);
	for (i = 0; i < NUM_MODS; i++) {
		manager.mod_pipes[i].pipefd = 0;
		manager.mod_pipes[i].mod_name = "";
	}
	if (!stat(MANAGER_SOCK_PATH, &st))
		diev("Manager already running.");
	init_manager_log_file();

	if (init_session_handler(&manager) < 0)
		diev("Error initalizing session handler!");

	/* We successfully set up the log file, we don't need stdin anymore */
	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0)
		diev("Failed to open '/dev/null' for stdin redirect!");
	if (dup2(nullfd, STDIN_FILENO) < 0)
		diev("Failed to redirect stdin to '/dev/null'");
	if (daemon(1, 1) < 0)
		diev("Error daemonizing");
	if (close(nullfd) < 0)
		log_err("Error closing nullfd");
	manager.listen_sock = setup_comm_socket();
}

/*
 * Cleanup and exit a finished module.
 * Finished means it either willingly exited, or it died.
 */
static void do_module_exit(struct module *m)
{
	sigset_t flags;
	int i;

	/* Nothing to do */
	if (module_is_parked(m))
		return;

	intr_save(flags);
	if (m->state == MODULE_RUNNING) {
		if (kill(m->pid, SIGTERM))
			kill(m->pid, SIGKILL);
		m->state = MODULE_DEAD;
		m->pid = -1;
	}
	manager.mods_dirty = 1;
	intr_restore(flags);

	for (i = 0; i < NUM_MODS; i++) {
		if (!strcmp(manager.mod_pipes[i].mod_name, m->mod_name)) {
			if (close(manager.mod_pipes[i].pipefd) < 0)
				logv_err("Error closing read pipe for module: %s",
						m->mod_name);
			manager.mod_pipes[i].mod_name = "";
			manager.mod_pipes[i].pipefd = 0;
			break;
		}
	}
	m->state = MODULE_EXITED;
}

/*
 * Manager shutdown routine.
 *
 * Main steps:
 * 	1. Disable interrupts.
 * 	2. Exit all running modules
 * 	3. Close and destroy the socket.
 * 	4. exit() the manager
 */
static void __noreturn shutdown_manager(void)
{
	sigset_t flags;
	int i;

	intr_save(flags);
	close_session_handler(&manager);
	for (i = 0; i < ARRAY_SIZE(mods); i++)
		do_module_exit(mods[i]);

	if (close(manager.listen_sock) < 0)
		logv_err("Error closing manager listen socket");
	if (unlink(MANAGER_SOCK_PATH) < 0)
		logv_err("Error removing manager socket from fs");
	log_info("Manager shutdown complete.");
	exit(0);
}

static void term_handler(int sign)
{
	(void) sign;
	manager.status = STOPPED;
}

/*
 * There are two main handlers to set up here.
 * 	1. Child death
 * 		- If a child dies we are going to want to try and recover their
 * 		    return status. Also attempt to restart them.
 * 	2. Termination Signal
 * 		- This signal means that the manager should stop. If the manager
 * 		    is stopping, then all the processes it is managing should
 * 		    also stop.
 */
static int init_sighands(void)
{
	struct sigaction act;
	sigset_t to_ignore;

	memset(&act, 0, sizeof(act));
	sigemptyset(&to_ignore);
	sigaddset(&to_ignore, SIGCHLD);
	act.sa_handler = child_death_handler;
	act.sa_mask = to_ignore;
	if (sigaction(SIGCHLD, &act, NULL) < 0)
		diev("Error setting SIGCHLD handler");

	memset(&act, 0, sizeof(act));
	act.sa_handler = term_handler;
	if (sigaction(SIGTERM, &act, NULL) < 0)
		diev("Error setting SIGTERM handler");
	return 0;
}

/*
 * __restart_mods - attempts to startup modules marked in need of a restart
 *
 * If a module refuses to startup (aka failed to start many times) then we
 * will just ignore it, log it, and move on.
 */
static void __restart_mods(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mods); i++) {
		struct module *m = mods[i];

		if (!m->needs_restart)
			continue;

		log_info("%s has died with code (%d) attempting to restart",
				m->mod_name, m->wstatus);

		if (manager.status == STARTING) {
			log_err("%s failed on manager startup!", m->mod_name);
			m->needs_restart = 0;
			continue;
		}

		if (manager.status == STOPPED)
			return;

		/* Keep retrying to init the module */
		for (;;) {
			/* Have the module exit its current state before restarting it */
			do_module_exit(m);
			if (++m->num_fails < MAX_FAIL_FOR_STOP) {
				if (!do_module_init(m))
					break;
			} else {
				log_err("%s failed too many times. "
					"Leaving off", m->mod_name);
				break;
			}
		}
		/*
		 * We either failed too many times or the module is
		 * alive. Either way, we are done fiddling with this
		 * module.
		 */
		m->needs_restart = 0;
	}
}

/*
 * restart_mods - set up to restart modules
 *
 * Called only by the manager when it is alerted that one of it's children
 * died. To make sure that our restart routine picks up on all failed modules,
 * we disable SIGCHLD interrupts. Once we finish restarting, reenable the
 * interrupts. This flow guarentees we do not miss any dead modules.
 */
static void restart_mods(void)
{
	sigset_t flags;
	intr_save(flags);
	__restart_mods();
	manager.mods_dirty = 0;
	intr_restore(flags);
}

/*
 * do_module_restart - Restart a requested module
 */
static int do_module_restart(struct module *m)
{
	do_module_exit(m);
	return do_module_init(m);
}

static struct module *get_mod(char *req_mod)
{
	int i;
	for (i = 0; i < NUM_MODS; i++) {
		struct module *m = mods[i];
		if (!strcmp(m->mod_name, req_mod))
			return m;
	}
	return NULL;
}


/* New manager commands should be added to this enum */
enum manager_cmds {
	CMD_NONE,
	CMD_SHUTDOWN,
	CMD_RESTART_MOD,
	CMD_DISABLE_MOD,
	CMD_ENABLE_MOD,
};


static enum manager_cmds parse_command(const char *input)
{
	if (!strncmp(input, "stop", strlen("stop")))
		return CMD_SHUTDOWN;
	if (!strncmp(input, "restart", strlen("restart")))
		return CMD_RESTART_MOD;
	if (!strncmp(input, "disable", strlen("disable")))
		return CMD_DISABLE_MOD;
	if (!strncmp(input, "enable", strlen("enable")))
		return CMD_ENABLE_MOD;
	return CMD_NONE;
}

const char *manager_process_input(char *input)
{
	enum manager_cmds cmd;
	char *arg;
	int errv = 1;

	cmd = parse_command(input);
	if (cmd == CMD_NONE)
		return "Unknown command.";

	if (cmd == CMD_SHUTDOWN) {
		manager.status = STOPPED;
		return "Shutting down...";
	}

	input = strchr(input, ' ');
	if (!input)
		return "No argument given.";
	while ((arg = strsep(&input, " "))) {
		struct module *m;
		if (!*arg)
			continue;

		m = get_mod(arg);
		if (!m)
			continue;

		switch (cmd) {
		case CMD_RESTART_MOD:
			errv = do_module_restart(m);
			break;
		case CMD_DISABLE_MOD:
			do_module_exit(m);
			errv = 0;
			break;
		case CMD_ENABLE_MOD:
			errv = do_module_init(m);
			break;
		default:
			die("%s made impossible switch on cmd val (%d)",
					__func__, cmd);
			break;
		}
	}
	return !errv ? "OK" : "FAIL";
}

/*
 * read_mod_input - Read output from a running module
 *
 * Take the output of the module and write it to the log file.
 * Prepend the name of the module so we can distinguish who is talking.
 */
static void read_mod_input(int fd, const char *mod_name)
{
	char buf[2048];
	int nr, nw, bytes_left, buf_len;

	buf_len = sizeof(buf);
	memset(buf, 0, buf_len);
	nw = snprintf(buf, buf_len, "[%s] ", mod_name);
	if (nw < 0 || nw >= buf_len) {
		log_err("%s: %s module name is too long?", __func__, mod_name);
		nw = snprintf(buf, buf_len, "[???] ");
	}

	bytes_left = buf_len - nw;
	while ((nr = read(fd, buf + buf_len - bytes_left, bytes_left)) > 0) {
		bytes_left -= nr;
		if (!bytes_left) {
			/* Flush */
			write(STDOUT_FILENO, buf, buf_len);
			bytes_left = buf_len;
		}
	}
	/* bytes_left can not be 0 here */
	if (bytes_left < buf_len) {
		/* Final flush, also ensure that we have newlines */
		char *lf = memchr(buf, '\n', buf_len);
		if (!lf)
			buf[buf_len - bytes_left--] = '\n';
		write(STDOUT_FILENO, buf, buf_len - bytes_left);
	}
}

/*
 * try_service_sock - Attempt to service the file descriptor as the listen sock
 */
static int try_service_socket(int fd)
{
	if (fd == manager.listen_sock) {
		start_new_session(&manager);
		return 0;
	}
	return -1;
}

/*
 * try_service_module - Attempt to service the file descriptor as a module
 */
static int try_service_module(int fd)
{
	int i;

	for (i = 0; i < NUM_MODS; i++) {
		if (manager.mod_pipes[i].pipefd == fd) {
			read_mod_input(fd, manager.mod_pipes[i].mod_name);
			return 0;
		}
	}
	return -1;
}

/*
 * try_service_session - Attempt to service the file descriptor as a session
 */
static int try_service_session(int fd)
{
	struct session *s;

	s = get_session_by_fd(&manager, fd);
	if (!s)
		return -1;
	process_session(&manager, s);
	return 0;
}

static void early_module_startup(int bot, int server)
{
	if (bot)
		do_module_init(&ts_bot);
	if (server)
		do_module_init(&ts_webserver);
}


/*
 * service_fds - Attempt to service any file descriptors marked ready by poll()
 *
 * Since poll() just marks which file descriptor was ready, we need to match it
 * to a component of the manager (i.e. socket, module, or session).
 */
static void service_fds(struct pollfd *fds, int npoll, int nready)
{
	struct pollfd *fd;
	int i;

	for (i = 0, fd = fds; i < npoll && nready > 0; i++, fd++) {
		int errv, readyfd = fd->fd;

		if (!(fd->revents & POLLIN))
			continue;
		nready--;
		errv = try_service_socket(readyfd);
		if (!errv)
			continue;
		errv = try_service_module(readyfd);
		if (!errv)
			continue;
		errv = try_service_session(readyfd);
		if (!errv)
			continue;

		logv_err("Could not service fd (%d)!", readyfd);
	}
}

/*
 * setup_poll_fds - Prepare the poll() array of file descriptors
 *
 * We will poll() on:
 * 	+ The listen socket file descriptor
 * 	+ All module pipe file descriptors
 * 	+ All currently running client sessions (interactive session)
 */
static int setup_poll_fds(struct pollfd **fds)
{
	struct session *s;
	struct pollfd *p;
	int len, i;

	len = 1; /* Start at 1 for the listen sock */
	len += manager.session_handler->num_sessions;
	for (i = 0; i < NUM_MODS; i++) {
		struct module *m = mods[i];
		if (m->pid > 0)
			len += 1;
	}

	p = calloc(len, sizeof(*p));
	if (!p)
		diev("malloc() error");
	if (*fds)
		free(*fds);
	*fds = p;

	/* Poll on the listen sock */
	p->fd = manager.listen_sock;
	p->events = POLLIN;
	p++;

	/* Poll on all the current sessions */
	list_for_each_entry(s, &manager.session_handler->sessions, list) {
		log_info("Adding session fd (%d) to poll", s->comm_fd);
		p->fd = s->comm_fd;
		p->events = POLLIN;
		p++;
	}

	/* Poll the currently loaded mods */
	for (i = 0; i < NUM_MODS; i++) {
		if (manager.mod_pipes[i].pipefd) {
			log_info("Adding module fd (%d) to poll", manager.mod_pipes[i].pipefd);
			p->fd = manager.mod_pipes[i].pipefd;
			p->events = POLLIN;
			p++;
		}
	}
	return len;
}

/*
 * Check if the manager is "dirty".
 * Shorthand word for that some state has changed that needs to be accounted for.
 */
static inline int manager_is_dirty(void)
{
	return manager.mods_dirty ||
		manager.session_handler->sessions_dirty;
}

/*
 * manager_cleanup_dirt - Cleanup the dirty states so we correctly poll.
 *
 * If the manager is "dirty" that means a state has chagned in which we need
 * to account for. Most notably it needs to be accounted for in our call to
 * poll(). Events that make the manager dirty:
 * 	- starting or disabling modules
 * 	- starting or stopping sessions (dirty sessions handled by session handler)
 */
static void manager_cleanup_dirt(void)
{
	sigset_t flags;

	intr_save(flags);
	restart_mods();
	manager.session_handler->sessions_dirty = 0;
	intr_restore(flags);
}

/*
 * destory_poll_fds - Deallocate the array of poll file descriptors
 */
static void destroy_poll_fds(struct pollfd *fds)
{
	if (fds)
		free(fds);
}

/*
 * Main manager loop.
 *
 * At this point we have:
 * 	+ Loaded all requested modules (bot/server)
 * 	+ Initalized the log file
 * 	+ Daemonized
 * 	+ Have a working local Unix socket for IPC and receiving commands
 * 	+ Registered a child death signal handler
 *
 * Next step is to setup poll(). We call on setup_poll_fds() to populate
 * our array of file descriptors, then poll().
 *
 * If poll() returns a ready file descriptor, we move on to service it.
 *
 * Important things to remember:
 *  - We could possbily halt the execution of this loop from an event. More
 *  specifically, if a child process was to die. Therefore it is imperative
 *  to make sure that system calls being made handle EINTR - see errno(3).
 */
static void start_manager_loop(void)
{
	struct pollfd *fds = NULL;
	int nfds;

	nfds = setup_poll_fds(&fds);
	while (manager.status == RUNNING) {
		int readyfd;

		/* Check for any state change */
		if (manager_is_dirty()) {
			manager_cleanup_dirt();
			nfds = setup_poll_fds(&fds);
			continue;
		}

		readyfd = poll(fds, nfds, MAN_POLL_TIMEOUT);
		if (!readyfd)
			continue;
		if (readyfd < 0) {
			if (errno != EINTR)
				logv_err("poll fail");
			continue;
		}

		service_fds(fds, nfds, readyfd);
	}
	destroy_poll_fds(fds);
	shutdown_manager();
}

/*
 * Maybe I am missing something, but I personally think that SIGPIPE default
 * behavior of killing your program is just silly. Why does it not just return
 * -1 and set errno? Anyways, in the case of the manager and manager client, we
 * want this to be ignored.
 */
static void disable_sigpipe(void)
{
	struct sigaction sa = {
		.sa_handler = SIG_IGN,
	};

	if (sigaction(SIGPIPE, &sa, NULL) < 0)
		diev("Error disabling SIGPIPE");
}

int main(int argc, char **argv)
{
	int opt, should_init_bot = 0, should_init_server = 0;
	const char *self_name = argv[0];

	if (argc == 1)
		usage(self_name);

	disable_sigpipe();
	while ((opt = getopt(argc, argv, "abis:S:w")) != -1) {
		switch (opt) {
		case 'a':
			should_init_server = 1;
			should_init_bot = 1;
			break;
		case 'b':
			should_init_bot = 1;
			break;
		case 'i':
			start_interactive();
			return 0;
		case 's':
		case 'S':
			if (try_send((const char **) argv, optind))
				usage(self_name);
			return 0;
		case 'w':
			should_init_server = 1;
			break;
		case '?':
		default:
			usage(self_name);
			break;
		}
	}
	argv += optind;
	argc -= optind;
	if (*argv)
		usage(self_name);

	init_manager();

	/*
	 * When we are here we are daemonized and ready to start spinning up
	 * bots and servers and such.
	 */
	init_sighands();
	early_module_startup(should_init_bot, should_init_server);
	manager.status = RUNNING;
	start_manager_loop();
	return 0;
}
