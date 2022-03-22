#ifndef _MANAGER_H_
#define _MANAGER_H_
#include <time.h>

#ifndef SUN_LEN
# define SUN_LEN(su) \
	(sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define MAX_CMD_LEN 4096
#define NUM_MODS 2
#define MANAGER_SOCK_PATH "/tmp/ts_manager_sock"

struct session_handler;

enum manager_status {
	STARTING,
	RUNNING,
	STOPPED,
};

/*
 * manager struct
 *
 * Keeps track of the state of the running manager.
 */
struct manager {
	/* listen_sock - file descriptor for it's listening socket */
	int listen_sock;

	/*
	 * running - is the manager currently running. 0 or 1 only
	 * Marked as volatile because the value can be changed by the SIGTERM
	 * signal handler.
	 */
	volatile enum manager_status status;

	/*
	 * mods_dirty - Marked if the manager still needs to acknowledge the
	 * modules state have changed.
	 */
	volatile unsigned int mods_dirty;

	/* session_handler - The "manager" of sessions */
	struct session_handler *session_handler;

	/* startup_time - The time the manager started up */
	struct tm *startup_time;
};

/* Only here to expose functionality to the session_handler */
extern const char *manager_process_input(char *);

#endif
