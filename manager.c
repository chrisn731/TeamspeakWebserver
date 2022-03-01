/*
 * Teamspeak Manager
 *
 * The purpose of this is to manage and control the teamspeak bot
 * and webserver. It is able to automatically stop, restart, and
 * recover a crashed process.
 *
 * When spawed, it daemonizes itself and creates a socket in order to
 * receieve commands.
 *
 *
 *      init_manager()
 *           |
 *           v
 *      start_manager_loop() -> poll() -> read_mod_input()
 *           ^                     |            |
 *           |                     v            |
 *           |                read_socket()     |
 *           |                     |            |
 *           |_____________________|____________|
 */

/*
 * TODO:
 * 	- If a child dies, manager still knows about it's pipe. Therefore,
 * 		the pipe of communication MUST be cleared when the child dies.
 * 		Probably best if done in child_death_handler()
 *
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
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

#define __noreturn __attribute__((__noreturn__))

#define LOG_FILE_PATH "/tmp/ts_manager_log.txt"
#define MANAGER_SOCK_PATH "/tmp/ts_manager_sock"
#define WEBSERVER_PATH "./tswebserver"
#define BOT_PATH "./bot.py"

#define MAX_CMD_LEN 4096
#define LOG_BUF_SIZE (1 << 13)

#ifndef SUN_LEN
# define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define NUM_MODS 2

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

enum manager_status {
	STARTING,
	RUNNING,
	STOPPED,
};

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

/*
 * manager struct
 *
 * Keeps track of the state of the running manager.
 */
static struct {
	/* listen_sock - file descriptor for it's listening socket */
	int listen_sock;

	/*
	 * running - is the manager currently running. 0 or 1 only
	 * Marked as volatile because the value can be changed by the SIGTERM
	 * signal handler.
	 */
	volatile enum manager_status status;
	volatile unsigned int mods_need_restart;
	struct {
		const char *mod_name;
		int pipefd;
	} mod_pipes[NUM_MODS];
	struct tm *startup_time;
} manager;

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
 * do_init_module - fork and attempt to initialize module
 *
 * This function calls prctl() (which is linux specific BTW) so that our
 * initialized module dies off if the manager were to unexpectedly die.
 *
 * This function also sets up a communication pipe between the parent and
 * it's children. This is so we can nicely write down what they are saying
 * in a better format than having them write it themselves.
 */
static int do_init_module(struct module *mod)
{
	sigset_t flags;
	int cpid, i, pipefds[2];

	log_info("Attempting to start up '%s'", mod->mod_name);
	if (pipe(pipefds) < 0) {
		logv_err("Failed to set up pipe for %s", mod->mod_name);
		return -1;
	}

	for (i = 0; i < NUM_MODS && manager.mod_pipes[i].pipefd; i++)
		;
	if (i >= NUM_MODS) {
		log_err("%s: Manager has no open pipes for '%s' module!",
				__func__, mod->mod_name);
		return -1;
	}

	manager.mod_pipes[i].pipefd = pipefds[0];
	manager.mod_pipes[i].mod_name = mod->mod_name;

	intr_save(flags);
	cpid = fork();
	switch (cpid) {
	case -1:
		logv_err("Failed to fork for %s", mod->mod_name);
		return -1;
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
	return 0;
}

/*
 * TODO: Make init_bot && init_server reentrant so that
 * calling them from the signal handler is not unsafe. For
 * now, this will do.
 */
static void child_death_handler(int signum)
{
	pid_t dead_child;
	int i, status = 0;

	(void) signum;
	/* wait() on all dead children */
	while ((dead_child = waitpid(-1, &status, WNOHANG)) > 0) {
		for (i = 0; i < ARRAY_SIZE(mods); i++) {
			struct module *mod = mods[i];
			if (dead_child == mod->pid && module_is_running(mod)) {
				mod->state = MODULE_DEAD;
				mod->needs_restart = 1;
				mod->pid = -1;
				mod->wstatus = status;
				manager.mods_need_restart = 1;
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

	/* We successfully set up the log file, we don't need stdin anymore */
	nullfd = open("/dev/null", O_RDWR);
	if (nullfd < 0)
		diev("Failed to open '/dev/null' for stdin redirect!");
	if (dup2(nullfd, STDIN_FILENO) < 0)
		diev("Failed to redirect stdin to '/dev/null'");
	if (daemon(1, 1) < 0)
		diev("Error daemonizing");
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
 * 	1. Attempt to kindly TERMinate children (kill if stubborn)
 * 	2. Remove socket
 * 	3. exit()
 */
static void __noreturn shutdown_manager(void)
{
	int i;
	sigset_t flags;

	intr_save(flags);
	for (i = 0; i < ARRAY_SIZE(mods); i++) {
		struct module *m = mods[i];
		do_module_exit(m);
	}
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
				if (!do_init_module(m))
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
	manager.mods_need_restart = 0;
	intr_restore(flags);
}


static void req_restart_mod(const char *req_mod)
{
	int i;

	for (i = 0; i < NUM_MODS; i++) {
		struct module *m = mods[i];
		if (!strcmp(m->mod_name, req_mod)) {
			m->needs_restart = 1;
			manager.mods_need_restart = 1;
			return;
		}
	}
}

/* New manager commands should be added to this enum */
enum manager_cmds {
	CMD_NONE,
	CMD_SHUTDOWN,
	CMD_RESTART_MOD,
	CMD_DISABLE_MOD,
	CMD_ENABLE_MOD,
};

static enum manager_cmds manager_process_input(const char *input)
{
	const char *sep;
	size_t cmd_len;

	sep = strchr(input, ' ');
	if (!sep)
		sep = strchr(input, '\0');

	cmd_len = sep - input;
	if (!strncmp(input, "stop", cmd_len))
		return CMD_SHUTDOWN;
	if (!strncmp(input, "restart", cmd_len))
		return CMD_RESTART_MOD;
	if (!strncmp(input, "disable", cmd_len))
		return CMD_DISABLE_MOD;
	if (!strncmp(input, "enable", cmd_len))
		return CMD_ENABLE_MOD;
	return CMD_NONE;
}

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
 * read_socket - read in socket content
 *
 * We only get here if the socket fd got triggered from the poll within
 * start_manager_loop()
 */
static void read_socket(void)
{
	char buffer[MAX_CMD_LEN];
	int cfd, nr;

	cfd = accept(manager.listen_sock, NULL, NULL);
	if (cfd < 0) {
		if (errno != EINTR)
			logv_err("Error while trying to accept sock connection.");
		return;
	}

	/*
	 * It's /techinically/ not really an error if read returns 0,
	 * but if we did get 0 that means that the socket connection
	 * was closed.
	 *
	 * Thus, if the connection was closed and we were expecting to
	 * read some command we will just treat it as an error.
	 */
	nr = read(cfd, buffer, sizeof(buffer) - 1);
	if (nr <= 0) {
		logv_err("Error while reading from sock connection.");
		goto done;
	}
	buffer[nr] = '\0';

	switch (manager_process_input(buffer)) {
	case CMD_SHUTDOWN:
		manager.status = STOPPED;
		break;
	case CMD_NONE:
	default:
		log_info("Received: %s", buffer);
		break;
	}
done:
	if (close(cfd) < 0)
		logv_err("Error attempting to close sock connection.");

}

static int setup_poll_fds(struct pollfd *fds, size_t size)
{
	int i, open_mods = 0;

	memset(fds, 0, size);
	for (i = 0; i < NUM_MODS; i++) {
		struct pollfd *f = &fds[open_mods];
		if (manager.mod_pipes[i].pipefd) {
			f->fd = manager.mod_pipes[i].pipefd;
			f->events = POLLIN;
			open_mods++;
		}
	}
	log_info("poll will have %d open mods", open_mods);
	fds[open_mods].fd = manager.listen_sock;
	fds[open_mods].events = POLLIN;
	open_mods++;
	return open_mods;
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
 * Now enter an infinite loop that starts by calling accept() and waiting
 * for a command or event to come up. If a child dies, we *temporarily* leave
 * the main loop to restart any dead modules. After that procedure is finished
 * we reenable the manager.
 *
 * Important things to remember:
 *  - accept() is BLOCKING therefore as soon as we come into here the manager
 *  will be put to sleep by the kernel until a connection to its socket is
 *  attempted. Therefore, there should be little to no stress on the cpu from
 *  the manager.
 *
 *  - We could possbily halt the execution of this loop from an event. More
 *  specifically, if a child process was to die. Therefore it is imperative
 *  to make sure that system calls being made handle EINTR - see errno(3).
 */
static void start_manager_loop(void)
{
	struct pollfd fds[NUM_MODS + 1];
	int num_mods_running;

	num_mods_running = setup_poll_fds(fds, sizeof(fds));
	while (manager.status == RUNNING) {
		int readyfd, i;

		if (manager.mods_need_restart) {
			restart_mods();
			/* Module states have changed, update our poller */
			num_mods_running = setup_poll_fds(fds, sizeof(fds));
			continue;
		}

		readyfd = poll(fds, num_mods_running, -1);
		if (readyfd < 0) {
			if (errno != EINTR)
				logv_err("poll fail");
			continue;
		}

		for (i = 0; i < num_mods_running; i++) {
			if (fds[i].revents & POLLIN) {
				if (fds[i].fd == manager.listen_sock)
					read_socket();
				else
					read_mod_input(fds[i].fd,
						manager.mod_pipes[i].mod_name);
			}
		}
	}
	shutdown_manager();
}

/* Make sure that the commands we are going to send are correct */
static inline int sanitize_command(const char *cmd)
{
	if (!strcmp(cmd, "stop"))
		return 0;
	if (!strcmp(cmd, "enable") ||
	    !strcmp(cmd, "disable") ||
	    !strcmp(cmd, "restart"))
		return 1;
	if (!strcmp(cmd, "test2"))
		return 2;
	return -1;
}

/*
 * send_command - send a command to the daemonized manager
 */
static void send_command(const char *arg)
{
	struct sockaddr_un sa;
	struct stat st;
	int fd;
	size_t cmd_len;

	if (stat(MANAGER_SOCK_PATH, &st) < 0)
		diev("Manager not currently running");

	cmd_len = strlen(arg);
	if (cmd_len > MAX_CMD_LEN)
		die("%s is longer than the max command length", arg);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		diev("Error creating socket file descriptor");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		diev("Error connecting: Ensure that the manager is currently running");
	if (write(fd, arg, cmd_len) != cmd_len)
		diev("Error writing to socket.");
	if (close(fd) < 0)
		diev("Error closing socket fd");
}

static void build_message(char *b, const char **args, int num_extra)
{
	char *const end = b + MAX_CMD_LEN;

	b += snprintf(b, end - b, "%s", *args++);
	while (b < end && num_extra--)
		b += snprintf(b, end - b, " %s", *args++);
	if (b >= end)
		die("Inputted command is too long!");
}

static int try_send(const char **args, int optidx)
{
	/* optidx is the index of the NEXT arg, so -1 so we stay on target */
	const char **send_args = &args[optidx - 1];
	char *msg;
	int num_extra;

	if (!*send_args)
		return -1;

	/*
	 * send_args[0] -> The command/operation
	 * send_args[1..] -> Any extra arguments for that operation
	 */
	num_extra = sanitize_command(*send_args);
	if (num_extra < 0)
		return -1;

	msg = malloc(MAX_CMD_LEN);
	if (!msg)
		diev("malloc() error");
	build_message(msg, send_args, num_extra);
	send_command(msg);
	free(msg);
	return 0;
}

static void start_interactive(void)
{
	struct pollfd fd = {
		.fd = STDOUT_FILENO,
		.events = POLLIN,
	};
	struct stat st;
	const char *prompt = "[manager]$ ";
	char buf[4096];
	size_t prompt_len;
	int res;

	if (stat(MANAGER_SOCK_PATH, &st) < 0)
		diev("Manager not currently running");

	/*
	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		diev("Error creating socket file descriptor.");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		diev("Error connecting: Ensure that the manager is currently running");
	*/
	prompt_len = strlen(prompt);
	do {
		write(STDOUT_FILENO, prompt, prompt_len);
		res = poll(&fd, 1, -1);
		buf[read(STDIN_FILENO, buf, sizeof(buf) - 1) - 1] = '\0';
		if (!strcmp(buf, "quit"))
			break;
	} while (res > 0);
}

int main(int argc, char **argv)
{
	int opt, should_init_bot = 0, should_init_server = 0;
	const char *self_name = argv[0];

	if (argc == 1)
		usage(self_name);

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
	if (should_init_bot && do_init_module(&ts_bot))
		return 1;
	if (should_init_server && do_init_module(&ts_webserver))
		return 1;

	manager.status = RUNNING;
	start_manager_loop();
	return 0;
}
