#ifndef _SESSION_H_
#define _SESSION_H_
#include "manager.h"
#include "list.h"

/*
 * session - Holds all information related to processing session inputs
 */
struct session {
	/* list - List of all current sessions */
	struct list_node list;

	/* cmd_len - The length of the command/message being received */
	uint32_t cmd_len;

	/* bytes_read - The amount of bytes read so far */
	size_t bytes_read;

	/* comm_fd - The file descriptor to communicate through */
	int comm_fd;

	/* buf_tail - The next byte to be written to for successive reads() */
	char *buf_tail;

	/* buf - The message */
	char buf[MAX_CMD_LEN];
};

/*
 * session_handler - Abstraction that hides all the internals from the manager
 */
struct session_handler {
	/* sessions - All currently open sessions */
	struct list_node sessions;

	/* num_sessions - Number of sessions in the list */
	unsigned int num_sessions;

	/*
	 * sessions_dirty - Marked if the manager needs to take note of a
	 * change of state
	 */
	int sessions_dirty;
};

extern void *start_session_handler(void *);
extern void start_new_session(struct manager *);
extern void process_session(struct manager *man, struct session *s);
extern int init_session_handler(struct manager *man);
extern void close_session_handler(struct manager *man);
extern struct session *get_session_by_fd(struct manager *man, int fd);

#endif
