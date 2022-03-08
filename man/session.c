#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "list.h"
#include "manager.h"
#include "session.h"

#define MANAGER_SOCK_PATH "/tmp/ts_manager_sock"

/*
 * __start_new_session - Allocate resources for a new sesssion
 */
static int __start_new_session(struct session_handler *sh, int sock)
{
	struct session *new;

	new = malloc(sizeof(*new));
	if (!new)
		goto bad_mem;

	new->comm_fd = accept(sock, NULL, NULL);
	if (new->comm_fd < 0)
		goto bad_accept;

	new->cmd_len = 0;
	new->buf_tail = new->buf;
	list_add_post(&new->list, &sh->sessions);
	sh->num_sessions++;
	sh->sessions_dirty = 1;
	return 0;
bad_accept:
	free(new);
bad_mem:
	return -1;
}


/*
 * start_new_session - Entry function from the manager into session handler
 */
void start_new_session(struct manager *man)
{
	__start_new_session(man->session_handler, man->listen_sock);
}

/*
 * process_command - Process the input from a sesssion
 *
 * This is only invoked when all the bytes have been received!
 */
static void process_command(struct session *s)
{
	const char *resp;

	s->cmd_len = 0;
	s->buf_tail = s->buf;
	resp = manager_process_input(s->buf);
	write(s->comm_fd, resp, strlen(resp));
}

/*
 * __process_session - Read in the connection to a session
 */
static int __process_session(struct session_handler *sh, struct session *sess)
{
	int res;

	/*
	 * If cmd_len is 0, this is a new session or we are waiting for a
	 * new command
	 */
	if (!sess->cmd_len) {
		uint32_t len;
		res = read(sess->comm_fd, &len, sizeof(len));
		if (res <= 0)
			return 0;
		sess->cmd_len = ntohl(len);
		if (sess->cmd_len == -1 || sess->cmd_len >= MAX_CMD_LEN)
			return 0;
		sess->bytes_read = 0;
	}

	if (sess->cmd_len) {
		uint32_t header_size = sizeof(sess->cmd_len);

		res = read(sess->comm_fd, sess->buf_tail, MAX_CMD_LEN - header_size - 1);
		if (res <= 0)
			return 0;

		sess->buf_tail += res;
		sess->bytes_read += res;
		*sess->buf_tail = '\0';
		if (sess->bytes_read == sess->cmd_len)
			process_command(sess);
	}
	return 1;
}

/*
 * close_session - Close an active session.
 *
 * Close the communication file descriptor and remove it from the list of sessions
 */
static void close_session(struct session_handler *sh, struct session *s)
{
	close(s->comm_fd);
	list_del(&s->list);
	free(s);
	sh->num_sessions--;
	sh->sessions_dirty = 1;
}

/*
 * process_session - Callpoint for manager to process a session
 */
void process_session(struct manager *man, struct session *s)
{
	struct session_handler *sh = man->session_handler;

	if (!__process_session(sh, s))
		close_session(sh, s);
}

/*
 * get_session_by_fd - Return a pointer to a session, looked up by its fd
 */
struct session *get_session_by_fd(struct manager *man, int fd)
{
	struct session_handler *sh = man->session_handler;
	struct session *s;

	list_for_each_entry(s, &sh->sessions, list) {
		if (s->comm_fd == fd)
			return s;
	}
	return NULL;
}

/*
 * close_session_handler - Teardown the session handler and release resources
 */
void close_session_handler(struct manager *man)
{
	struct session_handler *sh = man->session_handler;
	struct session *s, *to_free;

	list_for_each_entry_safe(to_free, s, &sh->sessions, list)
		close_session(sh, s);
}

/*
 * init_session_handler - Allocate resources and set up list for session handling
 */
int init_session_handler(struct manager *man)
{
	man->session_handler = calloc(1, sizeof(struct session_handler));
	if (!man->session_handler)
		return -1;
	init_list_node(&man->session_handler->sessions);
	return 0;
}
