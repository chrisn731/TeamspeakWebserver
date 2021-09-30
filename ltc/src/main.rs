use chrono::NaiveDateTime;
use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};
use std::env;
use std::fs::{self, File};
use std::io::prelude::*;
use std::io::BufReader;

/*
 * struct client - keeps track of important connection information and
 * different identification methods.
 */
#[derive(Eq)]
struct Client {
    /* name: Most recent name the client has used on the teamspeak. */
    name: String,

    /*
     * last_time_connected: Keeps track of when the most recent time the
     * client connected to the server.
     */
    last_time_connected: i64,

    /*
     * total_time_connected: Keeps track of the total time spent connected
     * across multiple disconnects.
     */
    total_time_connected: i64,

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
    num_conn: u32,
}

impl Ord for Client {
    fn cmp(&self, other: &Self) -> Ordering {
        self.total_time_connected.cmp(&other.total_time_connected)
    }
}

impl PartialOrd for Client {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for Client {
    fn eq(&self, other: &Self) -> bool {
        self.total_time_connected == other.total_time_connected
    }
}

struct ClientDatabase {
    /*
     * map - Hashmap that links unique client id's to a client's properties
     */
    map: HashMap<u32, Client>,
}

impl ClientDatabase {
    fn new() -> ClientDatabase {
        let map: HashMap<u32, Client> = HashMap::new();
        ClientDatabase { map }
    }

    /*
     * Log the time of a client connecting, and update their name
     * if they joined under some new alias
     */
    fn log_connect(&mut self, id: u32, name: &str, time: i64) {
        let mut client = match self.map.get_mut(&id) {
            Some(c) => c,
            None => {
                /* Insert the new client and grab the reference */
                self.map.insert(
                    id,
                    Client {
                        num_conn: 0,
                        last_time_connected: 0,
                        total_time_connected: 0,
                        name: name.to_owned(),
                    },
                );
                self.map.get_mut(&id).unwrap()
            }
        };

        client.num_conn += 1;
        if client.num_conn == 1 {
            client.last_time_connected = time;
            if client.name != name {
                client.name = name.to_owned();
            }
        }
    }

    /*
     * Log the time of a client disconncting.
     *
     * Notes:
     * There are two problems with teamspeak logs:
     *
     * 1.To me, it seems not all (dis)connections have been logged in very
     *  old logs.
     * 2. If someone joins a server twice (Austin and Austin1 are the same)
     *  and one of them leaves it registers them under the same client id.
     *  Therefore, need to make sure we update their total time connected once
     *  their last connection to the server has ended. See num_conn field in
     *  the Client struct for more info.
     *
     * These factors cause the parse to see things like:
     * 	(client) : disconnected
     * 	(client) : disconnected
     *
     * 	instead of...
     * 	(client) : connected
     * 	(client) : disconnected
     */
    fn log_disconnect(&mut self, id: u32, time: i64) {
        let mut client = match self.map.get_mut(&id) {
            Some(c) => c,
            None => {
                return;
            }
        };
        if client.num_conn != 0 {
            if client.num_conn == 1 && client.last_time_connected != 0 {
                client.total_time_connected += time - client.last_time_connected;
                client.last_time_connected = 0;
            }
            client.num_conn -= 1;
        }
    }

    /*
     * Everytime we switch to a new log file, the server had stopped/crashed and
     * restarted. Thus, no client should have their connection status completely
     * reset.
     */
    fn clear_clients(&mut self) {
        for c in self.map.values_mut() {
            c.num_conn = 0;
            c.last_time_connected = 0;
        }
    }
}

fn sort_and_print_client_times(db: ClientDatabase) {
    let mut clients: Vec<Client> = db.map.into_iter().map(|(_id, c)| c).collect();
    /*
     * Sort the clients in reverse order to make it easier for it to be parsed
     * when the output is picked up by the webserver
     */
    clients.sort_by(|a, b| b.total_time_connected.cmp(&a.total_time_connected));
    for c in clients.iter() {
        println!("{}\t{}", c.total_time_connected, c.name);
    }
}

#[derive(Eq)]
struct LogFile {
    creation_time: i64,
    name: String,
}

impl Ord for LogFile {
    fn cmp(&self, other: &Self) -> Ordering {
        /* Required for our log files to be used within a MinHeap */
        self.creation_time.cmp(&other.creation_time).reverse()
    }
}

impl PartialOrd for LogFile {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl PartialEq for LogFile {
    fn eq(&self, other: &Self) -> bool {
        self.creation_time == other.creation_time
    }
}

fn get_id(line: &String) -> u32 {
    let id = &line[line.find("id:").unwrap()..];
    let id_end = id.find(')').unwrap();
    id[3..id_end].parse::<u32>().unwrap()
}

fn get_client_name(name: &String) -> String {
    let name_start = name.find('\'').unwrap();
    let name_end = name.find("'(").unwrap();
    name[name_start + 1..name_end].to_owned()
}

enum ClientAction {
    Connect,
    Disconnect,
}

fn parse_line(line: &String, name: &mut String, idx: &mut u32) -> Option<ClientAction> {
    let action: ClientAction;

    if let Some(_i) = line.find("client connected") {
        action = ClientAction::Connect;
    } else if let Some(_i) = line.find("client disconnected") {
        action = ClientAction::Disconnect;
    } else {
        return None;
    }
    *name = get_client_name(line);
    *idx = get_id(line);
    return Some(action);
}

fn process_line(line: String, db: &mut ClientDatabase) {
    let time = match NaiveDateTime::parse_from_str(&line[..19], "%Y-%m-%d %H:%M:%S") {
        Ok(x) => x.timestamp(),
        Err(_) => {
            return ();
        }
    };

    let mut client_name = String::new();
    let mut id: u32 = 0;
    match parse_line(&line, &mut client_name, &mut id) {
        Some(ClientAction::Connect) => db.log_connect(id, &client_name, time),
        Some(ClientAction::Disconnect) => db.log_disconnect(id, time),
        None => (),
    }
}

fn parse_file(f: std::fs::File, db: &mut ClientDatabase) -> std::io::Result<()> {
    for line in BufReader::new(f).lines() {
        process_line(line?, db);
    }
    Ok(())
}

fn parse_file_list(mut logs: BinaryHeap<LogFile>, db: &mut ClientDatabase) -> std::io::Result<()> {
    for _i in 0..logs.len() {
        let log_file = logs.pop().unwrap();
        parse_file(File::open(log_file.name)?, db)?;
        db.clear_clients();
    }
    Ok(())
}

fn compile_logs(log_dir: &str) -> Result<BinaryHeap<LogFile>, std::io::Error> {
    let mut bh = BinaryHeap::new();
    for entry in fs::read_dir(log_dir)? {
        let de = entry?;
        let path = de.path();

        if path.is_dir() {
            continue;
        }

        let path_name = path.to_str().unwrap();
        if path_name.contains("_1.log") {
            let parse_time_str = NaiveDateTime::parse_from_str;
            let file_name = de.file_name().into_string().unwrap();
            let t = parse_time_str(&file_name[10..30], "%Y-%m-%d__%H_%M_%S")
                .unwrap()
                .timestamp();
            bh.push(LogFile {
                creation_time: t,
                name: path_name.to_owned(),
            });
        }
    }
    Ok(bh)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let logs = compile_logs(&args[1]).unwrap();
    let mut client_db = ClientDatabase::new();
    parse_file_list(logs, &mut client_db).expect("Failed to parse file list");
    sort_and_print_client_times(client_db);
}
