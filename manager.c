/*
 * Teamspeak Manager
 *
 * The purpose of this is to manage and control the teamspeak bot
 * and webserver. It is able to automatically stop, restart, and
 * recover a crashed process.
 *
 * When spawed, it daemonizes itself and creates a socket in order to
 * receieve commands.
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
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
#include <unistd.h>

#define LOG_FILE_PATH "/tmp/ts_manager_log.txt"
#define MANAGER_SOCK_PATH "/tmp/ts_manager_sock"
#define WEBSERVER_PATH "./tswebserver"
#define BOT_PATH "./bot.py"

#define MAX_CMD_LEN 4096
#define LOG_BUF_SIZE (1 << 13)

#define die(s, ...) __die("[FATAL] " s, ## __VA_ARGS__)
#define log_info(s, ...) __loginfo("[INFO] " s, ## __VA_ARGS__)
#define log_err(s, ...) __log_err("[ERR] " s, ## __VA_ARGS__)
#define NOTREACHED() \
	do {								\
		die("Not reached denoted line reached in %s : %d",	\
				__func__, __LINE__);			\
	} while (0)

#ifndef SUN_LEN
# define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define __noreturn __attribute__((__noreturn__))

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
	volatile unsigned int running;
	volatile unsigned int mods_need_restart;
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
	int wstatus;
};

static void init_ts_bot(void);
static struct module ts_bot = {
	.mod_name = "ts_bot",
	.pathname = BOT_PATH,
	.argv = {"python", BOT_PATH, NULL},
	.pid = -1,
	.init = &init_ts_bot,
	.num_fails = 0,
};

static void init_ts_webserver(void);
static struct module ts_webserver = {
	.mod_name = "ts_webserver",
	.pathname = WEBSERVER_PATH,
	.argv = {WEBSERVER_PATH, NULL},
	.pid = -1,
	.init = &init_ts_webserver,
	.num_fails = 0,
};
static struct module *mods[] = { &ts_bot, &ts_webserver };

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

enum log_levels {
	LOG_INFO,
	LOG_ERR,
	LOG_FATAL,
};

static void do_log(enum log_levels log_level, const char *fmt, va_list args)
{
	size_t nw;
	char *buffer = __log_buf;
	int errv = errno;

	nw = vsnprintf(buffer, LOG_BUF_SIZE, fmt, args);
	if (log_level == LOG_FATAL && errv)
		nw += snprintf(buffer + nw, LOG_BUF_SIZE - nw, " - %s",
							strerror(errv));
	buffer[nw] = '\n';

	write(STDOUT_FILENO, buffer, nw + 1);
}

/* Fatal error. Program execution should no longer continue. */
static void __noreturn __die(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	do_log(LOG_FATAL, fmt, argp);
	va_end(argp);
	exit(1);
}

/* Normal level logging */
static void __loginfo(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	do_log(LOG_INFO, fmt, argp);
	va_end(argp);
}

/*
 * Only purpose of this function is to make the code verbose in saying that
 * "Hey something pretty bad happened, but just write it down and keep going."
 */
static void __log_err(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	do_log(LOG_FATAL, fmt, argp);
	va_end(argp);
}

static void __noreturn init_ts_bot(void)
{
	execv("/bin/python", ts_bot.argv);
	log_err("[BOT ERR] - failed to startup bot");
	_exit(1);
}

static void __noreturn init_ts_webserver(void)
{
	if (chdir("./webserver") < 0)
		log_err("Could not change into webserver directory.");
	else
		execv(ts_webserver.pathname, ts_webserver.argv);
	log_err("[SERVER ERR] - failed to startup server");
	_exit(1);
}

/*
 * do_init_module - fork and attempt to initialize module
 *
 * This function calls prctl() (which is linux specific BTW) so that our
 * initialized module dies off if the manager were to unexpectedly die.
 */
static int do_init_module(struct module *mod)
{
	int cpid;

	log_info("Attempting to start up '%s'", mod->mod_name);
	cpid = fork();
	switch (cpid) {
	case -1:
		log_err("Failed to fork for %s", mod->mod_name);
		return -1;
	case 0:
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		mod->init(); /* DOES NOT RETURN */
		break;
	default:
		mod->pid = cpid;
		break;
	}
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
			if (dead_child == mod->pid) {
				mod->needs_restart = 1;
				mod->wstatus = status;
				mod->pid = -1;
				manager.mods_need_restart = 1;
			}
		}
	}
	if (dead_child < 0)
		die("Failed to wait for stopped child.");
}


/* Make sure that the commands we are going to send are correct */
static inline int sanitize_command(const char *cmd)
{
	return strcmp(cmd, "stop");
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
		die("Error opening/creating manager log file");
	if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0)
		die("dup2: Error duping");
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
	int sfd, on = 1;

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		die("Error creating socket file descriptor");
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0)
		die("Error setting SO_REUSEADDR failed for setsockopt");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (bind(sfd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		die("Error while attempting to bind");
	if (listen(sfd, 1) < 0)
		die("Error setting up socket to listen.");
	return sfd;
}

/*
 * init_manager - initalize log file, daemonzie, set up manager running state
 */
static void init_manager(void)
{
	struct stat st;

	if (!stat(MANAGER_SOCK_PATH, &st))
		die("Manager already running.");
	init_manager_log_file();
	if (daemon(1, 1) < 0)
		die("Error daemonizing");
	manager.listen_sock = setup_comm_socket();
	manager.running = 1;
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
		die("Manager not currently running");

	cmd_len = strlen(arg);
	if (cmd_len > MAX_CMD_LEN)
		die("%s is longer than the max command length", arg);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		die("Error creating socket file descriptor");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		die("Error connecting: Ensure that the manager is currently running");
	if (write(fd, arg, cmd_len) != cmd_len)
		die("Error writing to socket.");
	if (close(fd) < 0)
		die("Error closing socket fd");
}

/*
 * Kill wrapper to terminate children of the manager.
 * First attempts to be kind and terminate them. If that does not work
 * sends a SIGKILL as last resort.
 */
static int kill_pid(int pid)
{
	int err;

	err = kill(pid, SIGTERM);
	if (err)
		err = kill(pid, SIGKILL);
	return err;
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
	struct sigaction act = {
		.sa_handler = SIG_DFL
	};

	/* We are going to be killing children. Ignore SIGCHLD. */
	if (sigaction(SIGCHLD, &act, NULL) < 0)
		log_err("Error reseting child death handler.");

	for (i = 0; i < ARRAY_SIZE(mods); i++) {
		struct module *m = mods[i];

		if (m->pid > 0) {
			if (kill_pid(m->pid))
				log_err("Error killing '%s' with pid (%d)",
						m->mod_name, m->pid);
			else
				log_info("'%s' shutdown complete", m->mod_name);
		}
	}
	if (close(manager.listen_sock) < 0)
		log_err("Error closing manager listen socket");
	if (unlink(MANAGER_SOCK_PATH) < 0)
		log_err("Error removing manager socket from fs");
	log_info("Manager shutdown complete.");
	exit(0);
}

static void term_handler(int sign)
{
	(void) sign;
	manager.running = 0;
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
static int setup_sig_handlers(void)
{
	struct sigaction act;
	sigset_t to_ignore;

	memset(&act, 0, sizeof(act));
	sigaddset(&to_ignore, SIGCHLD);
	act.sa_handler = child_death_handler;
	act.sa_mask = to_ignore;
	if (sigaction(SIGCHLD, &act, NULL) < 0)
		return -1;

	memset(&act, 0, sizeof(act));
	act.sa_handler = term_handler;
	if (sigaction(SIGTERM, &act, NULL) < 0)
		return -1;
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

		/* Keep retrying to init the module */
		while (m->needs_restart) {
			log_info("%s has died with code (%d) attempting to restart",
					m->mod_name, m->wstatus);
			if (++m->num_fails < MAX_FAIL_FOR_STOP) {
				if (do_init_module(m))
					continue;
			} else {
				log_err("%s failed too many times. Leaving off",
						m->mod_name);
			}
			/*
			 * We either failed too many times or the module is
			 * alive. Either way, we are done fiddling with this
			 * module.
			 */
			m->needs_restart = 0;
		}
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
	sigset_t set, waiting;

	/* Do not let other children dying interrupt us */
	sigemptyset(&set);
	sigaddset(&set, SIGCHLD);
	sigprocmask(SIG_BLOCK, &set, NULL);
	__restart_mods();

	/*
	 * Check to see if another child died while we were restarting
	 * other dead modules
	 */
	sigpending(&waiting);
	if (!sigismember(&waiting, SIGCHLD))
		manager.mods_need_restart = 0;
	sigprocmask(SIG_UNBLOCK, &set, NULL);
}

/* New manager commands should be added to this enum */
enum manager_cmds {
	CMD_NONE,
	CMD_SHUTDOWN,
};

static enum manager_cmds manager_process_input(const char *input)
{
	if (!strcmp(input, "stop"))
		return CMD_SHUTDOWN;
	return CMD_NONE;
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
reenable:
	while (manager.running && !manager.mods_need_restart) {
		int cfd, nr;
		char buffer[MAX_CMD_LEN];

		cfd = accept(manager.listen_sock, NULL, NULL);
		if (cfd < 0) {
			if (errno != EINTR)
				log_err("Error while trying to accept sock connection.");
			continue;
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
			log_err("Error while reading from sock connection.");
			close(cfd);
			continue;
		}
		buffer[nr] = '\0';

		switch (manager_process_input(buffer)) {
		case CMD_SHUTDOWN:
			goto done;
			break;
		case CMD_NONE:
		default:
			break;
		}

		if (close(cfd) < 0)
			log_err("Error attempting to close sock connection.");
	}
	if (manager.running && manager.mods_need_restart) {
		restart_mods();
		goto reenable;
	}
done:
	shutdown_manager();
	NOTREACHED();
}

int main(int argc, char **argv)
{
	int opt, should_init_bot = 0, should_init_server = 0;
	const char *self_name = argv[0];

	if (argc == 1)
		usage(self_name);

	while ((opt = getopt(argc, argv, "abs:S:w")) != -1) {
		switch (opt) {
		case 'a':
			should_init_server = 1;
			should_init_bot = 1;
			break;
		case 'b':
			should_init_bot = 1;
			break;
		case 's':
		case 'S':
			if (sanitize_command(optarg))
				usage(self_name);
			send_command(optarg);
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
	if (setup_sig_handlers() < 0)
		die("Error setting up signal handlers.");
	if (should_init_bot && do_init_module(&ts_bot))
		return 1;
	if (should_init_server && do_init_module(&ts_webserver))
		return 1;

	start_manager_loop();
	return 0;
}
