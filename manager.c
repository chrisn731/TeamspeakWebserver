/*
 * Teamspeak Manager
 *
 * The purpose of this is to manage and control the teamspeak bot
 * and webserver. It is able to automatically stop, restart, and
 * recover a crashed process.
 *
 * When spawed, it daemonizes itself and creates a socket in order to
 * receieve commandsla
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
#include <sys/un.h>
#include <unistd.h>

#define LOG_FILE_PATH "/tmp/ts_manager_log.txt"
#define MANAGER_SOCK_PATH "/tmp/ts_manager_sock"
#define WEBSERVER_PATH "./tswebserver"
#define BOT_PATH "./bot.py"
#define MAX_CMD_LEN 4096

#define NOTREACHED()							\
	do {								\
		fprintf(stderr, "Not reached area reached in %s : %d",	\
				__func__, __LINE__);			\
	} while (0)

#ifndef SUN_LEN
# define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif

/*
 * Commented out for now bc idk the direction i wanna go
struct module_info {
	int pipefds[2];
	int pid;
};

static struct module_info ts_bot = {
	.pid = -1,
}
*/

static int bot_pid = -1;
static int server_pid = -1;


/* Fatal error. Program execution should no longer continue. */
static void die(const char *fmt, ...)
{
	int errv = errno;
	va_list argp;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fprintf(stderr, " - %s", strerror(errv));
	fputc('\n', stderr);
	exit(1);
}

static void usage(const char *self)
{
	fprintf(stdout,
		"Usage: %s [ ]\n",
		self);
	exit(1);
}

/*
 * Only purpose of this function is to make the code verbose in saying that
 * "Hey something pretty bad happened, but just write it down and keep going."
 * Also so I do not have to write "\n" every damn time.
 */
static void log_err(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
}

static int init_bot(void)
{
	char *const bot_argv[] = {"python", BOT_PATH, NULL};

	printf("Attempting to start up bot...\n");
	bot_pid = fork();
	switch (bot_pid) {
	case -1:
		log_err("Error forking while trying to init bot");
		return -1;
	case 0:
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		execv("/bin/python", bot_argv);
		log_err("[BOT ERR] - failed to startup bot");
		_exit(1);
		break;
	default:
		break;
	}
	return 0;
}

static int init_server(void)
{
	char *const server_argv[] = {WEBSERVER_PATH, NULL};

	printf("Attempting to start up server...\n");
	server_pid = fork();
	switch (server_pid) {
	case -1:
		log_err("Error forking while trying to init server");
		return -1;
	case 0:
		prctl(PR_SET_PDEATHSIG, SIGHUP);
		chdir("./webserver");
		execvp(WEBSERVER_PATH, server_argv);
		die("[SERVER ERR] - failed to startup server");
		_exit(1);
		break;
	default:
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
	if (bot_pid > 0 && kill(bot_pid, 0))
		init_bot();
	if (server_pid > 0 && kill(server_pid, 0))
		init_server();
}

static inline int setup_child_death_handler(void)
{
	struct sigaction act;

	act.sa_handler = child_death_handler;
	return sigaction(SIGCHLD, &act, NULL);
}

/* Make sure that the commands we are going to send are correct */
static int sanitize_command(const char *cmd)
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
	int sfd;

	sfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sfd < 0)
		die("Error creating socket file descriptor");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

retry:
	if (bind(sfd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0) {
		if (errno == EADDRINUSE) {
			remove(MANAGER_SOCK_PATH);
			goto retry;
		} else
			die("Error while attempting to bind");
	}
	if (listen(sfd, 1) < 0)
		die("Error setting up socket to listen.");
	return sfd;
}

static int init_manager(void)
{
	init_manager_log_file();
	if (daemon(1, 1) < 0)
		die("Error daemonizing");
	return setup_comm_socket();
}

static void send_command(const char *arg)
{
	struct sockaddr_un sa;
	int fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		die("Error creating socket file descriptor");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(fd, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		die("Error connecting.");

	if (strlen(arg) > MAX_CMD_LEN)
		die("%s is longer than the max command length", arg);
	if (write(fd, arg, strlen(arg)) != strlen(arg))
		die("Error writing to socket.");
	if (close(fd) < 0)
		die("Error closing socket fd");
}


static int kill_pid(int pid)
{
	int err;

	err = kill(pid, SIGTERM);
	if (err)
		err = kill(pid, SIGKILL);
	return err;
}

static void shutdown_manager(int sfd)
{
	struct sigaction act = {
		.sa_handler = SIG_DFL
	};

	if (sigaction(SIGCHLD, &act, NULL) < 0)
		log_err("Error reseting child death handler.");

	if (bot_pid > 0) {
		if (kill_pid(bot_pid) < 0)
			log_err("Error killing bot - pid: %d", bot_pid);
		else
			printf("Bot shutdown complete.\n");
	}

	if (server_pid > 0) {
		if (kill_pid(server_pid) < 0)
			log_err("Error killing server - pid: %d", server_pid);
		else
			printf("Server shutdown complete.\n");
	}
	if (remove(MANAGER_SOCK_PATH) < 0)
		log_err("Error removing sock from fs");
	exit(0);
}

enum {
	CMD_NONE,
	CMD_SHUTDOWN,
};

static int manager_process_input(const char *input)
{
	if (!strcmp(input, "stop"))
		return CMD_SHUTDOWN;
	return CMD_NONE;
}

static inline int manager_accept(int sfd)
{
	int cfd;
	do {
		cfd = accept(sfd, NULL, NULL);
	} while (errno == EINTR);
	return cfd;
}


static void start_manager_loop(int sfd)
{
	for (;;) {
		int cfd;
		char buffer[MAX_CMD_LEN];

		cfd = manager_accept(sfd);
		if (cfd < 0)
			die("Error while trying to accept sock connection.");

		memset(buffer, 0, sizeof(buffer));
		/*
		 * It's /techinically/ not really an error if read returns 0,
		 * but if we did get 0 that means that the socket connection
		 * was closed.
		 *
		 * Thus, if the connection was closed and we were expecting to
		 * read some command we will just treat it as an error.
		 */
		if (read(cfd, buffer, sizeof(buffer) - 1) <= 0)
			log_err("Error while reading from sock connection.");
		switch (manager_process_input(buffer)) {
		case CMD_SHUTDOWN:
			close(cfd);
			goto done;
		case CMD_NONE:
		default:
			break;
		}

		if (close(cfd) < 0)
			log_err("Error attempting to close sock connection.");
	}
done:
	shutdown_manager(sfd);
	NOTREACHED();
}


int main(int argc, char **argv)
{
	int opt, sfd, should_init_bot = 0, should_init_server = 0;
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

	sfd = init_manager();

	/*
	 * When we are here we are daemonized and ready to start spinning up
	 * bots and servers and such.
	 */

	if (setup_child_death_handler() < 0)
		die("Error setting up child death handler.");
	if (should_init_bot && init_bot())
		return 1;
	if (should_init_server && init_server())
		return 1;

	start_manager_loop(sfd);
	return 0;
}
