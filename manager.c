#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TS_BOT_NAME	"main.py"
#define PIPE_NAME 	"/tmp/ts_bot_pipe"
#define LOG_FILE_NAME	"/tmp/bot_manager_log"

#define MAX_CMD_LEN	4096
static FILE *log_file;

static void die(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	fprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(1);
}

#define LOG_NORM	0
#define LOG_DIE		1
static void _log(int type, const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	fprintf(log_file, fmt, argp);
	va_end(argp);
	fputc('\n', log_file);
	if (type == LOG_DIE) {
		fclose(log_file);
		exit(1);
	}
}

/*
 * Signal handler to clean up for if the bot dies. If it dies without us knowing,
 * clean up and shut down nicely.
 */
static void child_death_handler(int signnum)
{
	_log(LOG_NORM, "Child died unexpectedly. Shutting down.");
	fclose(log_file);
	unlink(PIPE_NAME);
	exit(1);
}

static int kill_bot(int pid)
{
	int err;

	_log(LOG_NORM, "Sending kill signal to bot.");
	if (signal(SIGCHLD, SIG_DFL) < 0)
		_log(LOG_NORM, "(%s) Failed to release signal handler.", __func__);
	err = kill(pid, SIGTERM);
	if (err) {
		/* We gave it a chance to end peacefully... */
		_log(LOG_NORM, "Error while sending SIGTERM, sending SIGKILL.");
		err = kill(pid, SIGKILL);
		if (err)
			_log(LOG_NORM, "Error while sending SIGKILL to bot.");
	}
	return err;
}

static int wait_for_cmd(int cid)
{
	int pipefd;
	struct pollfd pfds[1];

	if (signal(SIGCHLD, child_death_handler) < 0) {
		_log(LOG_NORM, "Failed to create signal handler for manager.");
		goto fail_signal;
	}

	pipefd = open(PIPE_NAME, O_RDONLY | O_NONBLOCK);
	if (pipefd < 0) {
		_log(LOG_NORM, "Failed to open pipe.");
		goto fail_open;
	}
	pfds->fd = pipefd;
	pfds->events = POLLIN;

	_log(LOG_NORM, "Manager successfully initalized. Starting to listen...");
	for (;;) {
		char buf[MAX_CMD_LEN];
		int nr;

		if (poll(pfds, 1, -1) < 0) {
			_log(LOG_NORM, "Failed to poll.");
			break;
		}

		nr = read(pfds->fd, buf, sizeof(buf) - 1);
		if (nr < 0) {
			_log(LOG_NORM, "Reading from pipe failed.");
			break;
		}

		if (!strcmp(buf, "close")) {
			_log(LOG_NORM, "Received close command. Shutting down bot.");
			break;
		}
		memset(buf, 0, nr);
	}
	close(pipefd);
fail_open:
	unlink(PIPE_NAME);
fail_signal:
	fclose(log_file);
	return kill_bot(cid);
}

static int init_manager(void)
{
	int status;
	pid_t pid;

	if (!access(PIPE_NAME, F_OK))
		die("Manager already running.");

	log_file = fopen(LOG_FILE_NAME, "w");
	if (!log_file)
		die("Failed to open log file.");

	_log(LOG_NORM, "Initializing manager...");
	pid = fork();
	if (!pid) {
		/*
		 * Double fork so we can detach and not create a zombie
		 */
		pid_t cpid = fork();
		if (!cpid) {
			/*
			 * Do not allow the bot or manager to link to any
			 * device / terminal.
			 */
			if (daemon(1, 0) < 0)
				_log(LOG_DIE, "Failed to daemonize.");
			if (mkfifo(PIPE_NAME, 0666) < 0)
				_log(LOG_DIE, "Failed to make fifo");

			cpid = fork();
			if (!cpid) {
				execl("/bin/python", "python", TS_BOT_NAME, NULL);
				_log(LOG_DIE, "ERROR: execl failed.");
			} else if (cpid > 0) {
				_exit(wait_for_cmd(cpid));
			} else {
				_log(LOG_DIE, "Failed to do final manager fork.");
			}
		}
		_exit(cpid > 0 ? 0 : 1);
	}
	if (pid < 0)
		die("Error forking.");
	if (waitpid(pid, &status, 0) < 0)
		die("Error while waiting for child (%d)", pid);
	if (!WIFEXITED(status))
		die("Child did not exit successfully while initializing manager.");
	return 0;
}

static int send_command(const char *cmd)
{
	int pipefd;

	if (access(PIPE_NAME, F_OK) < 0)
		die("Manager not currently running.");
	if (strlen(cmd) >= MAX_CMD_LEN)
		die("Command too long to be interpretd.");

	pipefd = open(PIPE_NAME, O_WRONLY | O_NONBLOCK);
	if (pipefd < 0)
		die("Pipe opening failed.");
	if (write(pipefd, cmd, strlen(cmd)) != strlen(cmd))
		die("Write to pipe error.");
	if (close(pipefd) < 0)
		die("Closing pipe error.");
	return 0;
}

static int restart_manager(void)
{
	send_command("close");
	return init_manager();
}

int main(int argc, char **argv)
{
	char *arg;

	if (argc == 1)
		return 0;
	while ((arg = argv[1]) != NULL) {
		if (*arg != '-')
			break;
		for (;;) {
			switch (*++arg) {
			case 'c':
			case 'C':
				return init_manager();
			case 'r':
			case 'R':
				return restart_manager();
			case 0:
			default:
				break;
			}
			break;
		}
		argv++;
	}
	return send_command(argv[1]);
}
