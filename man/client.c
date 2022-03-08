#include <arpa/inet.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "manager.h"

static void die(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(1);
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

/* cmd_is_empty - Ensure we are not sending blank commands */
static int cmd_is_empty(const char *buf)
{
	size_t buf_len;
	int i;

	buf_len = strlen(buf);
	for (i = 0; i < buf_len; i++) {
		if (isalnum(buf[i]))
			return 0;
	}
	return 1;
}

/*
 * connect_to_manager - Connect to the manager's unix socket
 * The location of this sock is defined within manager.h
 */
static int connect_to_manager(void)
{
	struct stat st;
	struct sockaddr_un sa;
	int sock;

	if (stat(MANAGER_SOCK_PATH, &st) < 0)
		die("Manager not currently running");

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		die("Error creating socket file descriptor.");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(sock, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		die("Error connecting: Ensure that the manager is currently running");
	return sock;
}

/*
 * send_cmd - Send a command to the manager
 * Layout of a message:
 * 	[0xSOMENUMB, ...]
 *
 * 	0xSOMENUMB -> First 32 bits of the message denote the length
 * 		of the message. (IN BIG EDIAN!)
 * 	Rest of the bytes are the message (NULL TERMINATOR NOT INCLUDED)
 */
static int send_cmd(char *buf, int bytes, int sock)
{
	int ret;
	uint32_t serial_bytes;

	if (cmd_is_empty(buf))
		return 0;

	serial_bytes = !strcmp(buf, "quit") ? -1 : htonl(bytes);
	memmove(buf + sizeof(serial_bytes), buf, bytes);
	memcpy(buf, &serial_bytes, sizeof(serial_bytes));
	write(sock, buf, bytes + sizeof(serial_bytes));
	if (serial_bytes != -1) {
		ret = read(sock, buf, MAX_CMD_LEN - 1);
		if (ret > 0) {
			buf[ret] = '\0';
			printf("%s\n", buf);
			return 0;
		}
	}
	return -1;
}

/*
 * build_message - Build a message to send to the manager
 *
 * This is used if the commands to send are on the command line
 * 	(stored within argv)
 */
static void build_message(char *b, const char **args, int num_extra)
{
	char *const end = b + MAX_CMD_LEN;

	b += snprintf(b, end - b, "%s", *args++);
	while (b < end && num_extra--)
		b += snprintf(b, end - b, " %s", *args++);
	if (b >= end)
		die("Inputted command is too long!");
}

/*
 * try_send - Try sending command line based command to the manager
 */
int try_send(const char **args, int optidx)
{
	/* optidx is the index of the NEXT arg, so -1 so we stay on target */
	const char **send_args = &args[optidx - 1];
	char *msg;
	int num_extra, sock;

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
		die("malloc() error");
	build_message(msg, send_args, num_extra);
	if (cmd_is_empty(msg))
		return 0;
	sock = connect_to_manager();
	send_cmd(msg, strlen(msg), sock);
	close(sock);
	free(msg);
	return 0;
}

/*
 * wait_next_command - Poll() stdin, waiting for a command to send
 */
static int wait_next_command(int sock)
{
	struct pollfd fds[2] = {
		{
			.fd = STDIN_FILENO,
			.events = POLLIN,
		},
		{
			.fd = sock,
			.events = POLLIN,
		}
	};
	const char *prompt = "[manager]$ ";
	size_t prompt_len = strlen(prompt);
	char *nl, buf[MAX_CMD_LEN];
	int ret;

	/* Prompt for input */
	write(STDOUT_FILENO, prompt, prompt_len);

	ret = poll(fds, 1, -1);
	if (ret <= 0)
		return !!ret;

	/* Read in the command */
	ret = read(STDIN_FILENO, buf, MAX_CMD_LEN - 1);
	if (ret < 0)
		return ret;
	buf[ret] = '\0';
	nl = strchr(buf, '\n');
	if (nl)
		*nl = '\0';
	return send_cmd(buf, ret, sock);
}

/*
 * start_interactive - Start an interactive session with the manager
 */
void start_interactive(void)
{
	struct stat st;
	struct sockaddr_un sa;
	int sock, errv;

	if (stat(MANAGER_SOCK_PATH, &st) < 0)
		die("Manager not currently running");

	sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0)
		die("Error creating socket file descriptor.");

	sa.sun_family = AF_LOCAL;
	strncpy(sa.sun_path, MANAGER_SOCK_PATH, sizeof(sa.sun_path));
	sa.sun_path[sizeof(sa.sun_path) - 1] = '\0';

	if (connect(sock, (struct sockaddr *) &sa, SUN_LEN(&sa)) < 0)
		die("Error connecting: Ensure that the manager is currently running");

	do {
		errv = wait_next_command(sock);
	} while (!errv);
	close(sock);
}
