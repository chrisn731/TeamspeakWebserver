#define _XOPEN_SOURCE /* Needed for strptime */
#define _DEFAULT_SOURCE /* d_type */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "list.h"
#include "list_sort.h"

#define MAX_LINE_SIZE	4096
#define MAX_CLIENT_NAME	128
#define MAX_FILE_PATH	512

#define __hot __attribute__((__hot__))
#define sizeof_field(type, member) (sizeof(((type *) 0)->member))

/*
 * struct client - keeps track of important connection information and
 * different identification methods.
 */
struct client {
	/*
	 * last_time_connected: Keeps track of when the most recent time the
	 * client connected to the server.
	 */
	time_t last_time_connected;

	/*
	 * total_time_connected: Keeps track of the total time spent connected
	 * across multiple disconnects.
	 */
	time_t total_time_connected;

	/*
	 * list: List structure to form a list of every client that has ever
	 * connected.
	 */
	struct list_node list;

	/* id: Unique identification number given to every client. */
	int id;

	/*
	 * num_conn: The number of concurrent connections the client currently
	 * has. e.g, if 'Bob' joins under the name 'Bob' and then rejoins the
	 * same server 'Bob1' will show up in the logs with the same id.
	 * This leaves us with:
	 * 	'Bob' (last_time_connected: 30)
	 * 	'Bob1' (last_time_connected: 65) <- We don't want that time.
	 * However, we don't want to lost track of the total time connected
	 * if one of these two connections disconnect. This member solves this
	 * issue.
	 */
	unsigned int num_conn;

	/* name: Most recent name the client has used on the teamspeak. */
	char name[MAX_CLIENT_NAME];
};

struct log_file {
	time_t time;
	struct list_node list;
	char name[MAX_FILE_PATH];
};

static time_t time_constraint;
static LIST_NODE(client_list);
static LIST_NODE(log_list);
static unsigned int time_in_seconds;
static unsigned int tail_count, head_count;

static void die(const char *fmt, ...)
{
	va_list argp;
	int errval = errno;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	if (errval)
		fprintf(stderr, ": %s", strerror(errval));
	fputc('\n', stderr);
	exit(1);
}

static void print_warn(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	fputc('\n', stderr);
}


static void die_usage(const char *prog_name)
{
	fprintf(stdout,
		"Usage: %s [-d MM-DD-YYYY] [-s] [-t | -h #] log_directory\n"
		"Available flags\n"
		"  -d		Constraint Date MM-DD-YYYY\n"
		"  -s 		Print time connected in seconds\n"
		"  -t -h        Print a set number of clients from the\n"
		"                tail [-t] or head [-h]\n",
		prog_name);
	exit(0);
}

static __hot void log_conn(const char *client_name, int id, time_t t)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list) {
		if (c->id == id) {
			/*
			 * Clients that recently connect are likely to show up
			 * again. Move them to the front of the list to reduce
			 * time iterating down the list in future calls.
			 */
			list_move(&c->list, &client_list);
			goto found;
		}
	}
	/* Client was not found in the list, add them. */
	c = calloc(sizeof(*c), 1);
	if (!c)
		die("Memory alloc error.");
	c->id = id;
	list_add_post(&c->list, &client_list);
found:
	if (++c->num_conn == 1) {
		c->last_time_connected = t;
		if (strncmp(c->name, client_name, MAX_CLIENT_NAME)) {
			/*
			 * If the name of the client is different from what we
			 * have currently saved, update their name so we always
			 * have the most recent version of someone's name.
			 */
			strncpy(c->name, client_name, MAX_CLIENT_NAME);
		}
	}
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
static __hot void log_disconn(int id, time_t t_disconn)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list) {
		if (c->id == id && c->num_conn) {
			if (c->last_time_connected && c->num_conn == 1) {
				c->total_time_connected +=
					t_disconn - c->last_time_connected;
				c->last_time_connected = 0;
			}
			c->num_conn--;
			return;
		}
	}
}

static int get_name(const char *buf, char *t, int buf_len)
{
	int nr = 0;

	/*
	 * When we enter in here, *buf should be pointing to an apostrophe.
	 * Thus, increment the buffer to pass it and get to the name.
	 */
	if (*buf == '\'') {
		buf++;
		nr++;
	}
	for (; nr < buf_len; nr++) {
		/*
		 * End of name is denoted by: '(
		 * That is the start of the id string
		 */
		if (*buf == '\'' && buf[1] == '(')
			break;
		*t++ = *buf++;
	}
	return nr;
}

static long get_id(const char *buf)
{
	long val;
	char *endp;

	/*
	 * When we enter here: buf -> '(id:#) so,
	 * buf + 5 -> # (the number we want)
	 */
	buf += 5;
	val = strtol(buf, &endp, 10);
	if (val == LONG_MIN || val == LONG_MAX || endp == buf || *endp != ')')
		return -1;
	return val;
}

#define NO_ACTION 0x00
#define CLIENT_CONNECT 0x01
#define CLIENT_DISCONNECT 0x02

/*
 * parse_line - parse words within the line looking for some keywords
 *
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
 *
 * Once the line has been completely read, we can use this information
 * to update the client list.
 */
static int parse_line(const char *buf, char *name_buf, int *id)
{
	int action;
	char *action_str;

	action_str = strstr(buf, "client connected");
	if (action_str) {
		action = CLIENT_CONNECT;
		buf = action_str + strlen("client connected ");
	} else {
		action_str = strstr(buf, "client disconnected");
		if (action_str)
			action = CLIENT_DISCONNECT;
		else
			return NO_ACTION;
		buf = action_str + strlen("client disconnected ");
	}

	buf += get_name(buf, name_buf, MAX_CLIENT_NAME);
	*id = get_id(buf);
	return action;
}

/*
 * str_to_time - Convert time string to seconds since Unix Epoch
 *
 * UTC_DIFF is the hour difference from Universal Time Coordinated.
 * East coast is 5 hours.
 * Sidenote:
 * This function was made because mktime() works when you call it once.
 * In this case, we call it hunderds of thousands of times causing mktime()
 * to bottleneck the entire program.
 *
 * Using mktime() runtime average: 1.171s
 * Using str_to_time() runtime average: 0.119s
 */
#define UTC_DIFF 5
static time_t str_to_time(const char *time_str, const char *fmt)
{
	struct tm tm = {0};
	long tyears, tdays, leaps, utc_hrs;
	const int mon_days[] = {
		31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
	};
	int i;

	if (!strptime(time_str, fmt, &tm))
		return -1;

	tyears = tm.tm_year - 70;
	leaps = (tyears + 2) / 4;
	for (i = 0, tdays = 0; i < tm.tm_mon; i++)
		tdays += mon_days[i];

	tdays += tm.tm_mday - 1;
	tdays = tdays + (tyears * 365) + leaps;
	utc_hrs = tm.tm_hour + UTC_DIFF;
	return (tdays * 86400) + (utc_hrs * 3600) + (tm.tm_min * 60) + tm.tm_sec;
}

/*
 * process_line - process a single line of a file
 *
 * The purpose of this function is to act on whatever parse_line returned. If
 * we found all relevant information from parse_line, we can follow up and start
 * marking clients as connected or disconnected. Otherwise, we can skip the line.
 */
static void process_line(const char *buf)
{
	time_t time;
	char client_name[MAX_CLIENT_NAME] = {0};
	int id, action;

	action = parse_line(buf, client_name, &id);
	/* id = 1 is serveradmin, disregard him */
	if (action == NO_ACTION || id == 1)
		return;

	if (id <= 0) {
		print_warn("Failed to get id! Line: %s", buf);
		return;
	}

	time = str_to_time(buf, "%Y-%m-%d %H:%M:%S");
	if (time == -1 || time < time_constraint) {
		print_warn("Failed to parse time for line: %s", buf);
		return;
	}

	switch (action) {
	case CLIENT_CONNECT:
		log_conn(client_name, id, time);
		break;
	case CLIENT_DISCONNECT:
		log_disconn(id, time);
		break;
	default:
		die("bad action");
	case NO_ACTION:
		break;
	}
}

/*
 * parse_file - begin parsing each line of a given file
 *
 * Reads a file, line by line, and hands off the buffer to process_line()
 *
 * If the line is too long to fit into the buffer, it prints the line it was
 * unable to parse and exits.
 */
static int parse_file(FILE *fp)
{
	char buf[MAX_LINE_SIZE];

	while (fgets(buf, MAX_LINE_SIZE, fp)) {
		if (!memchr(buf, '\n', MAX_LINE_SIZE))
			die("Line that starts with %.*s is too long to parse. "
				"Consider increasing MAX_LINE_SIZE",
				MAX_LINE_SIZE - 1, buf);
		process_line(buf);
	}
	return feof(fp) ? 0 : 1;

}

#define SECS_IN_HOUR (3600)
#define SECS_IN_DAY (SECS_IN_HOUR * 24)
static void print_client_time(const struct client *c)
{
	unsigned long secs = c->total_time_connected;

	if (time_in_seconds) {
		printf("%lu", secs);
	} else {
		unsigned long days, hrs, mins;

		days = secs / SECS_IN_DAY;
		secs -= days * SECS_IN_DAY;
		hrs = secs / SECS_IN_HOUR;
		secs -= hrs * SECS_IN_HOUR;
		mins = secs / 60;
		secs -= mins * 60;
		printf("%lud %luh %lum %lus", days, hrs, mins, secs);
	}
	printf("\t%s\n", c->name);
}

static void print_client_list(void)
{
	struct client *c;

	if (tail_count) {
		list_for_each_entry(c, &client_list, list) {
			print_client_time(c);
			if (!--tail_count)
				break;
		}
	} else if (head_count) {
		list_for_each_entry_reverse(c, &client_list, list) {
			print_client_time(c);
			if (!--head_count)
				break;
		}
	} else {
		list_for_each_entry(c, &client_list, list)
			print_client_time(c);
	}
}

/*
 * add_to_file_list - add's a log file to log list
 *
 * Adds a file to the log list. The name of each file should have the datetime
 * of when it was created. This allows us to make sure to leave out log files
 * if they are outside the 'date constraint' given by commmand line args.
 */
static void add_to_file_list(const char *full_path, const char *fn)
{
	struct log_file *l, *new;
	time_t log_ctime;

	log_ctime = str_to_time(fn, "ts3server_%Y-%m-%d__%H_%M_%S");
	if (log_ctime == (time_t) -1) {
		print_warn("Failed to add %s to parse list. Times will be off!", fn);
		return;
	}

	list_for_each_entry(l, &log_list, list) {
		/* Sort the files from oldest to newest */
		if (l->time > log_ctime)
			break;
	}
	new = malloc(sizeof(*new));
	if (!new)
		die("Memory alloc error.");
	new->time = log_ctime;
	strncpy(new->name, full_path, MAX_FILE_PATH);
	list_add_prev(&new->list, &l->list);
}

/*
 * compile_logs - traverse log directory and create file list
 *
 * Traverses log directory and for each valid log file calls add_to_file_list()
 */
static void compile_logs(const char *dir)
{
	DIR *dp;
	struct dirent *de;
	size_t dir_len, remaining_bytes;
	char file_path[MAX_FILE_PATH];

	dp = opendir(dir);
	if (!dp)
		die("Failed to open directory '%s'", dir);

	dir_len = snprintf(file_path, MAX_FILE_PATH, "%s%s",
			dir, dir[strlen(dir) - 1] == '/' ? "" : "/");
	if (dir_len < 0)
		die("snprintf() error");
	remaining_bytes = MAX_FILE_PATH - dir_len - 1; /* 1 reserved for '\0' */

	while ((de = readdir(dp)) != NULL) {
		/*
		 * Only files ending in _1.log have actual meaningful data to
		 * parse.
		 */
		if (!strstr(de->d_name, "_1.log"))
			continue;
		if (de->d_type == DT_REG) {
			if (strlen(de->d_name) >= remaining_bytes)
				die("The file path %s%s is too large.",
							file_path, de->d_name);
			strcpy(file_path + dir_len, de->d_name);
			add_to_file_list(file_path, de->d_name);
		}
	}
	if (closedir(dp))
		die("Error closing %s", dir);
}

/*
 * Everytime we switch to a new log file, the server had stopped/crashed and
 * restarted. Thus, no client should have their connection status completely
 * reset.
 */
static void reset_clients(void)
{
	struct client *c;

	list_for_each_entry(c, &client_list, list) {
		c->num_conn = 0;
		c->last_time_connected = 0;
	}
}

static void parse_file_list(void)
{
	struct log_file *lf;

	list_for_each_entry(lf, &log_list, list) {
		FILE *fp;

		fp = fopen(lf->name, "r");
		if (!fp)
			die("Error opening '%s'", lf->name);
		if (parse_file(fp) || !feof(fp))
			die("Failed to fully parse '%s'", lf->name);
		reset_clients();
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

static int list_cmp(const struct list_node *a, const struct list_node *b)
{
	const struct client *c1 = (void *) a - offsetof(struct client, list);
	const struct client *c2 = (void *) b - offsetof(struct client, list);

	return c1->total_time_connected - c2->total_time_connected;
}

static long get_arg_val(const char *input, char option)
{
	char *endptr;
	long val;

	val = strtol(input, &endptr, 10);
	if (endptr == input)
		die("Error parsing input '%s' for option '%c'", input, option);
	if (val <= INT_MIN || val >= INT_MAX)
		die("Input '%lu' is too large for option '%c'", val, option);
	return val;
}

int main(int argc, char **argv)
{
	struct tm tm;
	int opt;
	const char *prog_name = argv[0];

	while ((opt = getopt(argc, argv, "d:h:st:")) != -1) {
		switch (opt) {
		case 'd':
			memset(&tm, 0, sizeof(tm));
			if (!strptime(optarg, "%m-%d-%Y", &tm)) {
				print_warn("Failed to parse -d argument '%s'", optarg);
				die_usage(prog_name);
			}
			time_constraint = mktime(&tm);
			break;
		case 's':
			time_in_seconds = 1;
			break;
		case 'h':
			if (tail_count)
				die_usage(prog_name);
			head_count = get_arg_val(optarg, opt);
			break;
		case 't':
			if (head_count)
				die_usage(prog_name);
			tail_count = get_arg_val(optarg, opt);
			break;
		case '?':
		default:
			die_usage(prog_name);
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (!*argv)
		die_usage(prog_name);

	compile_logs(*argv);
	parse_file_list();
	list_sort(&client_list, &list_cmp);
	print_client_list();
	free_logs();
	free_client_list();
	return 0;
}
