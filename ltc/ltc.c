#define _XOPEN_SOURCE /* Needed for strptime */
#define _DEFAULT_SOURCE /* d_type */
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "list.h"

#define MAX_LINE_SIZE	4096
#define MAX_CLIENT_NAME	128

#define sizeof_field(type, member) (sizeof(((type *) 0)->member))

struct client {
	time_t last_time_connected;
	time_t total_time_connected;
	char name[MAX_CLIENT_NAME];
	struct list_node list;
	int id;
};

struct log_file {
	time_t time;
	char name[512];
	struct list_node list;
};

static LIST_NODE(client_list);
static LIST_NODE(log_list);

static void die(const char *fmt, ...)
{
	va_list argp;
	va_start(argp, fmt);
	fprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
	exit(1);
}

static void log_conn(const char *client_name, int id, time_t t)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list) {
		if (c->id == id)
			goto found;
	}
	/* Client was not found in the list, add them. */
	c = calloc(sizeof(*c), 1);
	if (!c)
		die("Memory alloc error.");
	c->id = id;
	list_add_post(&c->list, &client_list);
found:
	if (strcmp(c->name, client_name)) {
		/*
		 * If the name of the client is different from what we have
		 * currently saved, update their name so we always have the
		 * most recent version of someone's name.
		 */
		strncpy(c->name, client_name, MAX_CLIENT_NAME - 1);
	}
	c->last_time_connected = t;
}

/*
 * log_diconn - update client node with duration they were connected
 * @client_name: name of client that disconnected
 * @t: time that they disconnected
 *
 * Notes:
 * There seems to be a strange problem with very old teamspeak logs. It
 * seems not all (dis)connections have been logged. This causes the parser to
 * see things like:
 * 	(client) : disconnected
 * 	(client) : disconnected
 *
 * 	instead of...
 * 	(client) : connected
 * 	(client) : disconnected
 *
 * This strange behavior causes c->total_time += time_discon - c->last_conn_time
 * to accidentally grow very large. The best solution I can work out is just
 * ignore these values because we can't reliably tell when they actually
 * connected. Thus, on every disconnection set their last connection time to 0
 * so if we come across consecutive disconnects the data won't be too crazy.
 *
 * Beware when using names and identifications when searching for clients in
 * the list. Need to keep an eye out for people logging in with a name that
 * is not actually related to them.
 * 	For example,
 * 		'Bob' has id 1 and 'Alice' has id 2
 * 		If 'Bob' logs in to the teamspeak with the name 'Alice' then
 * 		searching by name will cause the algorithm to add Bob's time
 * 		spent on the server to Alice's time.
 */
static void log_disconn(const char *client_name, int id, time_t t)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list) {
		if (c->id == id) {
			if (c->last_time_connected) {
				c->total_time_connected += t - c->last_time_connected;
				c->last_time_connected = 0;
			}
			return;
		}
	}
}

static int get_log_type(const char *buf, char *t)
{
	int nr = 0;

	if (*buf == '|') {
		buf++;
		nr++;
	}
	while (*buf != '|' && !isspace(*buf)) {
		nr++;
		*t++ = *buf++;
	}
	while (isspace(*buf++))
		nr++;
	return nr;
}

static int get_word(const char *buf, char *t, int buf_len)
{
	int nr = 0;

	while (nr++ < buf_len) {
		char c = *buf++;
		if (isspace(c))
			break;
		*t++ = c;
	}
	return nr;
}

/*
 * Has a void return value because the name is the last piece of information
 * we need from the buffer.
 */
static int get_name(const char *buf, char *t, int buf_len)
{
	int nr = 0;

	/*
	 * When we enter in here, *buf should be pointing to an apostrophe.
	 * Thus, increment the buffer to pass it and get to the name.
	 */
	if (*buf == '\'')
		buf++;
	while (nr++ < buf_len) {
		/* Only allow ascii characters. */
		if (*buf > 0) {
			if (*buf == '\'' && buf[1] == '(')
				break;
			*t++ = *buf;
		}
		buf++;
	}
	return nr;
}

static void get_id(const char *buf, char *t, int buf_len)
{
	int nr;

	/*
	 * When we enter here: buf -> '(id:#) so,
	 * buf + 5 -> # (the number we want)
	 */
	buf += 5;
	for (nr = 0; isdigit(*buf) && nr < buf_len; buf++, nr++)
		*t++ = *buf;

}

#define ACTION_LEN 20
static int parse_line(const char *buf, char *name_buf, char *action, int *id)
{
	char log_type[10] = {0};
	char log_event[20] = {0};
	char first[20] = {0};
	char id_buf[10] = {0};

	/* Skip past the date shit */
	while (*buf && *buf != '|')
		buf++;
	buf += get_log_type(buf, log_type);

	/*
	 * There are a bunch of other log "events" (idk if that is what they
	 * are actually called, but I will call them that). Client connections
	 * and disconnections are _only_ logged under the event
	 * "VirtualServerBase". So, if we read something other than that we can
	 * move past that line.
	 */
	if (strcmp(log_type, "INFO"))
		return -1;
	buf += get_log_type(buf, log_event);
	if (strcmp(log_event, "VirtualServerBase"))
		return -1;
	while (!isalpha(*buf))
		buf++;
	buf += get_word(buf, first, sizeof(first));
	if (strcmp(first, "client"))
		return -1;
	buf += get_word(buf, action, ACTION_LEN);
	if (strcmp(action, "connected") && strcmp(action, "disconnected"))
		return -1;
	buf += get_name(buf, name_buf, MAX_CLIENT_NAME);
	get_id(buf, id_buf, sizeof(buf));
	*id = atoi(id_buf);
	return 0;
}

static void process_data(const char *buf)
{
	struct tm tm = {0};
	time_t time;
	char client_name[MAX_CLIENT_NAME] = {0};
	char action[ACTION_LEN] = {0};
	int id;

	/*
	 * This is basically a long winded process to just parse
	 * a single line of the teamspeak log. I don't wanna use sscanf
	 * because:
	 * 	(a) sscanf just has a bad history of doing evil things,
	 * 		such as becoming exponential (GTA)
	 * 	(b) sscanf just feels clunky to use with trying to parse a
	 * 		string that is not always laid out the same.
	 *
	 * We need to read the
	 * 	- Time and convert it into seconds since the Unix Epoch.
	 * 	- Other useless information on the line
	 * 	- Client name
	 * Once the line has been completely read, we can use this information
	 * to update the client list.
	 */

	/* First grab the time of the log time */
	if (!strptime(buf, "%Y-%m-%d %H:%M:%S", &tm))
		return;
	time = mktime(&tm);
	if (time == (time_t) -1)
		return;
	if (parse_line(buf, client_name, action, &id))
		return;

	if (!strcmp(action, "connected"))
		log_conn(client_name, id, time);
	else if (!strcmp(action, "disconnected"))
		log_disconn(client_name, id, time);
	else
		fprintf(stderr, "WARN - action come up as: %s\n", action);
}


static int parse_file(FILE *fp)
{
	char buf[MAX_LINE_SIZE];

	while (fgets(buf, MAX_LINE_SIZE, fp))
		process_data(buf);
	return feof(fp) ? 0 : 1;

}

static void print_client_list(void)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list)
		printf("%lu %s\n", c->total_time_connected, c->name);
}

static void add_to_file_list(const char *full_path, const char *fn)
{
	struct log_file *l, *new;
	struct tm tm = {0};
	time_t log_ctime;

	if (!strptime(fn, "ts3server_%Y-%m-%d__%H_%M_%S", &tm))
		die("strptime failed on: %s", fn);
	log_ctime = mktime(&tm);
	if (log_ctime == (time_t) -1)
		return;
	list_for_each_entry(l, &log_list, list) {
		/* Sort the files from oldest to newest */
		if (l->time > log_ctime)
			break;
	}
	new = malloc(sizeof(*new));
	if (!new)
		die("Memory alloc error.");
	new->time = log_ctime;
	strcpy(new->name, full_path);
	list_add_prev(&new->list, &l->list);
}

static void compile_logs(const char *dir)
{
	DIR *dp;
	struct dirent *de;

	dp = opendir(dir);
	if (!dp)
		die("Failed to open directory '%s'", dir);

	while ((de = readdir(dp)) != NULL) {
		char file_path[1024];

		/*
		 * Only files ending in _1.log have actual meaningful data to
		 * parse.
		 */
		if (!strstr(de->d_name, "_1.log"))
			continue;

		memset(file_path, 0, sizeof(file_path));
		sprintf(file_path, "%s%s", dir, de->d_name);
		if (de->d_type == DT_REG)
			add_to_file_list(file_path, de->d_name);
	}
	if (closedir(dp))
		die("Error closing %s", dir);
}

static void begin_parsing(void)
{
	struct log_file *lf;

	list_for_each_entry(lf, &log_list, list) {
		FILE *fp;

		fp = fopen(lf->name, "r");
		if (!fp) {
			fprintf(stderr, "Error fopen on %s\n", lf->name);
			continue;
		}
		if (parse_file(fp))
			die("Failed to parse '%s'", lf->name);
		if (fclose(fp))
			fprintf(stderr, "Error closing %s\n", lf->name);
	}
}

static void free_logs(void)
{
	struct log_file *to_free, *cursor;

	list_for_each_entry_safe(to_free, cursor, &log_list, list)
		free(to_free);
}

static void free_client_list(void)
{
	struct client *to_free, *cursor;

	list_for_each_entry_safe(to_free, cursor, &client_list, list)
		free(to_free);
}

int main(int argc, char **argv)
{
	char *dir_path;

	if (argc != 2)
		die("Usage: %s [log directory]", argv[0]);

	/*
	 * Need to make sure the directory we want to parse ends in
	 * a '/' so that when we create path names it doesnt get screwed
	 * up.
	 */
	dir_path = calloc(sizeof(*dir_path), strlen(argv[1]) + 2);
	if (!dir_path)
		die("Memory alloc error.");
	strncpy(dir_path, argv[1], strlen(argv[1]));
	if (argv[1][strlen(argv[1]) - 1] != '/')
		dir_path[strlen(argv[1])] = '/';

	compile_logs(dir_path);
	free(dir_path);
	begin_parsing();
	print_client_list();
	free_logs();
	free_client_list();
	return 0;
}
