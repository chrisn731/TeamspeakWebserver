#include <bits/stdc++.h>
#include <time.h>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

/*
 * Basic representation of a teamspeak log file
 */
class LogFile {
public:
	LogFile(time_t t, const std::string &n) : time(t), path(n) { }

	LogFile(time_t t, const std::string &&n) : time(t), path(std::move(n)) { }

	bool operator<(const LogFile &b) const {
		return time < b.time;
	}

	bool operator>(const LogFile &b) const {
		return time > b.time;
	}

	const std::string& file_path(void) const {
		return path;
	}

private:
	/* time: Time the file was created */
	time_t time;

	/* path: Path to the file in the directory */
	std::string path;
};

/*
 * Representation of a client connecting to the server.
 */
class Client {
public:
	Client(const std::string &nickname, time_t time) :
		last_time_connected(time),
		total_time_connected(0),
		num_conn(1),
		name(nickname)
	{ }

	Client(const std::string &&nickname, time_t time) :
		last_time_connected(time),
		total_time_connected(0),
		num_conn(1),
		name(std::move(nickname))
	{ }

	void log_conn(const std::string &&logged_name, time_t t) {
		/*
		 * ONLY update the client if this is a fresh connection to
		 * the server.
		 */
		if (++num_conn == 1) {
			last_time_connected = t;
			if (name.compare(logged_name))
				name = std::move(logged_name);
		}
	}

	/*
	 * Notes about the if's in this function:
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
	 */
	void log_disconn(time_t t) {
		if (num_conn) {
			if (last_time_connected && num_conn == 1) {
				total_time_connected += t - last_time_connected;
				last_time_connected = 0;
			}
			num_conn--;
		}
	}

	/* Reset the client's connection fields */
	void reset(void) {
		num_conn = 0;
		last_time_connected = 0;
	}

	bool operator<(const Client &c) const {
		return total_time_connected < c.total_time_connected;
	}

	bool operator>(const Client &c) const {
		return total_time_connected > c.total_time_connected;
	}

	void print_client_time(bool time_in_seconds) const {
		time_t secs = total_time_connected;
		time_t SECS_IN_HOUR = 3600;
		time_t SECS_IN_DAY = SECS_IN_HOUR * 24;

		if (time_in_seconds) {
			std::cout << secs;
		} else {
			unsigned long days, hrs, mins;

			days = secs / SECS_IN_DAY;
			secs -= days * SECS_IN_DAY;
			hrs = secs / SECS_IN_HOUR;
			secs -= hrs * SECS_IN_HOUR;
			mins = secs / 60;
			secs -= mins * 60;
			std::cout
				<< days << "d "
				<< hrs  << "h "
				<< mins << "m "
				<< secs << "s";
		}
		std::cout << "\t" << name << "\n";
	}

private:
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
	std::string name;
};

/*
 * Database of all client connections
 */
class ClientDatabase {
	using client_id = unsigned int;
public:
	void log_conn(const std::string &&name, client_id id, time_t t) {
		auto res = client_map.find(id);

		if (res != client_map.end())
			res->second.log_conn(std::move(name), t);
		else
			client_map.insert(
				std::pair<client_id, Client>(id, Client(std::move(name), t))
			);
	}

	/*
	 * Update client node with duration they were connected
	 */
	void log_disconn(client_id id, time_t t) {
		auto res = client_map.find(id);

		if (res != client_map.end())
			res->second.log_disconn(t);
	}

	/*
	 * Reset all client's connections. This is important to do because there
	 * are logs in which not all clients are shown disconnecting before the
	 * end of the file. This behavior (I believe) is due to the fact that
	 * the server could have crashed or forced shutdown.
	 */
	void reset_clients(void) {
		for (auto it = client_map.begin(); it != client_map.end(); it++)
			it->second.reset();
	}

	std::unordered_map<client_id, Client>::const_iterator begin(void) const {
		return client_map.begin();
	}

	std::unordered_map<client_id, Client>::const_iterator end(void) const {
		return client_map.end();
	}
private:
	/* client_map: Unordered mapping of unique client id's to a Client */
	std::unordered_map<client_id, Client> client_map;
};

struct ProgArgs {
	time_t time_constraint;
	unsigned int tail_count;
	unsigned int head_count;
	bool time_in_seconds;

	ProgArgs(void) :
		time_constraint(0),
		tail_count(0),
		head_count(0),
		time_in_seconds(false)
	{ }
};

static ClientDatabase db;

#define UTC_DIFF 5
static time_t str_to_time(const char *time_str, const char *fmt)
{
	struct tm tm = {0};
	long tyears, tdays, leaps, utc_hrs;
	/*
	 * days_past_since_jan[x] = # days past in the year where
	 * 	x: [0, 11] denoting months since janurary.
	 */
	const int days_past_in_year[] = {
		31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365
	};

	if (!strptime(time_str, fmt, &tm))
		return -1;

	tyears = tm.tm_year - 70;
	leaps = (tyears + 2) / 4;
	/* tm_mon represents num months since janurary (0 - 11) */
	tdays = tm.tm_mon > 0 ? days_past_in_year[tm.tm_mon - 1] : 0;
	tdays += tm.tm_mday - 1;
	tdays = tdays + (tyears * 365) + leaps;
	utc_hrs = tm.tm_hour + UTC_DIFF;
	return (tdays * 86400) + (utc_hrs * 3600) + (tm.tm_min * 60) + tm.tm_sec;
}

static std::vector<LogFile> compile_logs(const std::string &dir)
{
	std::vector<LogFile> logs;
	for (const auto& entry : std::filesystem::directory_iterator(dir)) {
		time_t log_ctime;
		std::string file_name{entry.path().u8string()};
		size_t last_slash;

		if (file_name.find("_1.log") == std::string::npos)
			continue;
		last_slash = file_name.rfind("/");
		log_ctime = str_to_time(
			file_name.c_str() + last_slash + 1,
			"ts3server_%Y-%m-%d__%H_%M_%S");
		if (log_ctime < 0) {
			std::cerr
				<< "Failed to parse time for file '"
				<< file_name
				<< "'!\n";
			exit(1);
		}
		logs.emplace_back(log_ctime, std::move(file_name));
	}
	sort(logs.begin(), logs.end());
	return logs;
}

static void get_name(const std::string_view &line, std::string &name)
{
	size_t name_start, name_end, npos = std::string::npos;

	/*
	 * Names in the logs are surrounded by ''
	 */
	name_start = line.find("'") + 1;
	name_end = line.find("'", name_start);
	if (name_start == npos || name_end == npos)
		throw std::runtime_error("Failed to parse name!");
	name = line.substr(name_start, name_end - name_start);
}

static void get_id(const std::string_view &line, int &id)
{
	size_t id_start, id_end, npos = std::string::npos;

	/*
	 * ID's in the logs are formatted in the logs like: (id:##)
	 * where ## is the ID
	 */
	id_start = line.find("(id:") + std::strlen("(id:");
	id_end = line.find(")", id_start);
	if (id_start == npos || id_end == npos)
		throw std::runtime_error("Failed to find id on line!");
	auto result = std::from_chars(line.data() + id_start, line.data() + id_end, id);
	if (result.ec == std::errc::invalid_argument)
		throw std::runtime_error("Failed to parse id from text!");
}

enum class ClientAction {
	NO_ACTION,
	CLIENT_CONNECT,
	CLIENT_DISCONNECT,
};

/*
 * parse_line - parse the action, name, and id of the client in a line
 */
static ClientAction parse_line(const std::string &line, std::string &name, int &id)
{
	ClientAction a;
	size_t pos;
	std::string_view view;

	pos = line.find("client connected");
	if (pos != std::string::npos) {
		a = ClientAction::CLIENT_CONNECT;
		view = std::string_view(line.data() + pos + std::strlen("client connected"));
	} else {
		pos = line.find("client disconnected");
		if (pos == std::string::npos)
			return ClientAction::NO_ACTION;
		view = std::string_view(line.data() + pos + std::strlen("client disconnected"));
		a = ClientAction::CLIENT_DISCONNECT;
	}

	try {
		get_name(view, name);
		get_id(view, id);
	} catch (std::runtime_error &e) {
		std::cerr << e.what() << '\n';
		std::cerr << "\tLine that failed: " << line << '\n';
		a = ClientAction::NO_ACTION;
	}
	return a;
}

/*
 * process_line - Begin processing a single line from the file
 *
 * We need to read the
 * 	- Time and convert it into seconds since the Unix Epoch.
 * 	- Client name
 * 	- Client id
 * 	- Client action (connection or disconnection)
 *
 * Once the line has been completely read, we can use this information
 * to update the client.
 */
static void process_action_on_line(const std::string &line, time_t time_constraint)
{
	ClientAction action;
	time_t time;
	std::string client_name;
	int id = 0;

	action = parse_line(line, client_name, id);
	if (action == ClientAction::NO_ACTION || id == 1)
		return;

	if (id <= 0) {
		std::cout << "Failed to parse id! Line: " << line << '\n';
		return;
	}
	time = str_to_time(line.c_str(), "%Y-%m-%d %H:%M:%S");
	if (time == -1 || time < time_constraint)
		return;

	switch (action) {
	case ClientAction::CLIENT_CONNECT:
		db.log_conn(std::move(client_name), id, time);
		break;
	case ClientAction::CLIENT_DISCONNECT:
		db.log_disconn(id, time);
		break;
	case ClientAction::NO_ACTION:
	default:
		break;
	}
}

static void parse_file(std::ifstream &file, time_t time_constraint)
{
	std::string line;
	while (std::getline(file, line))
		process_action_on_line(line, time_constraint);
}

static void parse_files(const std::vector<LogFile> &logs, struct ProgArgs args)
{
	for (const auto &l : logs) {
		std::ifstream infile(l.file_path());
		parse_file(infile, args.time_constraint);
		infile.close();
		db.reset_clients();
	}
}

static long get_arg_val(const char *input, char option)
{
	char *endptr;
	long val;

	val = strtol(input, &endptr, 10);
	if (endptr == input) {
		std::cout << "Error parsing input '" << input
			<< "' for option '" << option << "'\n";
		exit(1);
	}
	if (val <= INT_MIN || val >= INT_MAX) {
		std::cout << "Input '" << val << "is too large for option '"
			<< option << "'\n";
		exit(1);
	}
	return val;
}

int main(int argc, char *argv[])
{
	std::vector<Client> client_vec;
	std::vector<LogFile> log_vec;
	struct ProgArgs args;
	int opt;

	while ((opt = getopt(argc, argv, "d:h:st:")) != -1) {
		struct tm tm;
		switch (opt) {
		case 'd':
			memset(&tm, 0, sizeof(tm));
			if (!strptime(optarg, "%m-%d-%Y", &tm)) {
				std::cout
					<< "Failed to parse -d argument '"
					<< optarg
					<< "'\n";
				exit(1);
			}
			args.time_constraint = mktime(&tm);
			break;
		case 's':
			args.time_in_seconds = true;
			break;
		case 'h':
			if (args.tail_count) {
				std::cout << "Can not use both 'h' and 't' flags\n";
				exit(1);
			}
			args.head_count = get_arg_val(optarg, opt);
			break;
		case 't':
			if (args.head_count) {
				std::cout << "Can not use both 'h' and 't' flags\n";
				exit(1);
			}
			args.tail_count = get_arg_val(optarg, opt);
			break;
		case '?':
		default:
			break;

		}
	}
	argc -= optind;
	argv += optind;

	/*
	 * Ensure that after all flags are parsed, we still have the
	 * log directory passed in
	 */
	if (!*argv) {
		std::cout << "Please input log directory\n";
		exit(1);
	}

	log_vec = compile_logs(*argv);
	parse_files(log_vec, args);

	for (auto it = db.begin(); it != db.end(); it++)
		client_vec.push_back(std::move(it->second));
	/* Want clients in order of greatest to least */
	sort(client_vec.begin(), client_vec.end(), std::greater<Client>());

	if (args.head_count) {
		for (auto c = client_vec.begin();
				c != client_vec.end() && args.head_count--; c++)
			c->print_client_time(args.time_in_seconds);
	} else if (args.tail_count) {
		for (auto c = client_vec.rbegin();
				c != client_vec.rend() && args.tail_count--; c++)
			c->print_client_time(args.time_in_seconds);
	} else {
		for (auto c = client_vec.rbegin(); c != client_vec.rend(); c++)
			c->print_client_time(args.time_in_seconds);
	}
	return 0;
}
