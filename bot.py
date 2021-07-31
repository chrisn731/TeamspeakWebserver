# C C C C C C C C C C C C

# TODO:
#   1) Divide and conqueor our files? [main.py, filefinder.py, evan.py, dice.py]

import ts3
import time
import sys
import os
import subprocess
import sqlite3
import random

SID = 1
# Must be edited to suit your workstation.
URI = "telnet://serveradmin:IUy8lW5R@localhost:10011"
FILE_DIR = "C:\\Users\\jyuen\\Desktop\\Goods\\Programming\\python\\TS_Bot\\teamspeak3-server_win64\\files\\virtualserver_1\\"
SAYINGS_DIR = "C:\\Users\\jyuen\\Desktop\\Goods\\Programming\\python\\TS_Bot\\sayings\\"

# Teamspeak Connection Class
# A nice wrapper around some of the commonly used commands to stop from
# having to deal with the wierd ts3conn.exec_() parameters and such.
# Should also make the code look overall more coherent.
class tsc:
    def __init__(self, ts3conn):
        self.ts3conn = ts3conn
        self.ts3conn.exec_("use", sid=SID)

    def listen_to_global_chat(self):
        return self.ts3conn.exec_("servernotifyregister", event="textserver")

    def global_msg(self, gmsg):
        return self.ts3conn.exec_("gm", msg=gmsg)

    def wait_for_event(self, timeout):
        return self.ts3conn.wait_for_event(timeout=timeout)

    def get_clientlist(self):
        return self.ts3conn.query("clientlist").all()

    def get_channellist(self):
        return self.ts3conn.query("channellist").all()

    def send_keepalive(self):
        self.ts3conn.send_keepalive()


sayings = {}
tsconn = None

for (dirpath, dirnames, filenames) in os.walk(SAYINGS_DIR):
    for filename in filenames:
        with open(SAYINGS_DIR + filename, "r") as lines:
            person = filename.split(".")[0].lower()
            sayings_list = []
            for line in lines:
                sayings_list.append(line)
            sayings[person] = sayings_list

def file_finder(item):
    # Create a found files
    Directory = ""
    Found = []
    for (dirpath, dirnames, filenames) in os.walk(FILE_DIR):
        for filename in filenames:
            if item in filename:
                Found.append(filename)
                Directory = dirpath

    tokens = Directory.split("_")

    # If you find more than one file
    if len(Found) != 1:
        # Return null if no files found
        if(len(Found) == 0):
            return 0
        # Don't want to overload the chat with files so just return a too many results error
        elif len(Found) > 3:
            return 4
        # if files found is 2 or 3 return the files found to be more specific
        else:
            tsconn.global_msg("I found some files of similar name:")
            for files in Found:
                tsconn.global_msg("{}".format(files))

    return tokens[len(tokens) - 1]

def say(person):
    lines = sayings.get(person, 0)
    if lines:
        line = lines[random.randint(0,len(lines)-1)]
        tsconn.global_msg(line)
    else:
        tsconn.global_msg("Cannot find sayings for " + person)

def roll_dice(data):
    dice = data["dice"]
    user = data["user"]

    numbers = dice.split("d")
    if len(numbers) != 2 or not numbers[0].isnumeric() or not numbers[1].isnumeric():
        tsconn.global_msg("Roll Usage: !roll {# of dice}d{# of sides on die}")
        return

    num_die = int(numbers[0])
    die_val = int(numbers[1])

    # Handle cases of funny people
    if num_die < 1:
        tsconn.global_msg("Throw atleast 1 die!")
        return

    if num_die > 1000:
        tsconn.global_msg("Too many dice!")
        return

    if die_val < 1:
        tsconn.global_msg("The universe has imploded!")
        return

    if die_val > 100:
        tsconn.global_msg("That die is too large!")
        return

    output = "{user} rolled {dice} for {total}!"
    sum_string = ""
    total = 0

    for i in range(num_die):
        r = random.randint(1, die_val)
        sum_string += str(r)
        if i != num_die - 1:
            sum_string += " + "

        total += r

    output += " (" + sum_string + ")"
    tsconn.global_msg(output.format(user=user, dice=dice, total=total))

def database_search(channelid):
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()
    c.execute("SELECT * FROM channel_properties;")
    channelname = "error"

    rows = c.fetchall()
    for row in rows:
        if channelid in row and 'channel_name' in row:
            channelname = (row[len(row) - 1])

    conn.commit()
    conn.close()
    return channelname


def countdown(data):
    timer = int(data["time"])
    user = data["user"]

    # Handle cases of funny people
    if timer <= 0:
        tsconn.global_msg("Fuck you")
        return

    if timer > 1000:
        tsconn.global_msg("Fuck you")
        return

    while timer != 0:
        output = "{user}'s Timer: {timer}"
        tsconn.global_msg(output.format(user=user, timer=timer))
        time.sleep(1)
        timer -= 1

    output = "{user}'s Timer Is Up"
    tsconn.global_msg(output.format(user=user))


def start_file_finder(data):
    # Find the ID of the channel so we know where to look in the database
    file_to_find = data["file"]
    user = data["user"]
    channelid = file_finder(ts3conn, tokenized[1])

    # ChannelID > 0, so if < 0 the file wont exit
    if channelid == 0:
        tsconn.global_msg("I'm sorry, {}, but that file does not seem \
                        to exist.".format(user))
    elif channelid == 4:
        tsconn.global_msg("I'm sorry, {}, but you need to be more specific.\
                        Too many results returned".format(user))
    else:
        # Connect to the database and look through the channel_properties
        Channel_Name = database_search(int(channelid))
        if Channel_Name == "error":
            tsconn.global_msg("Sorry, {}, There was an issue acccessing \
                            the server database.".format(user))

        # Do something based on what the user wants
        elif "!find" in message:
            tsconn.global_msg("That file seems to be located in: {}".format(Channel_Name))
        else:
            pass


def do_ltc(data):
    user = data["user"]
    tokens = data["tokens"]

    if len(tokens) != 2 or tokens[0] != "-d":
        tsconn.global_msg("Usage: !ltc -d MM-DD-YYYY")
        return

    try:
        date = datetime.strptime(tokens[1], "%m-%d-%Y")
    except ValueError:
        # This is raised if the message given does not follow the MM-DD-YYYY format.
        tsconn.global_msg("Usage: !ltc -d MM-DD-YYYY")
        return

    p = subprocess.run(['../stats/ltc', '-d', tokens[1], "../stats/logs"],
                                    capture_output=True, text=True)
    if p.returncode != 0:
        tsconn.global_msg("Usage: !ltc -d MM-DD-YYYY")
        return
    for line in p.stdout.splitlines():
        tsconn.global_msg(line)


def start():
    tsconn.listen_to_global_chat()
    while True:
        tsconn.send_keepalive()
        try:
            event = tsconn.wait_for_event(timeout=180)
        except ts3.query.TS3TimeoutError:
            # If we get here, ts3conn.wait_for_event() stalled for the full
            # 60 seconds. No need to do anything.
            pass
        else:
            print(event[0])
            # We don't want to spend time interpreting what the server is saying.
            if (event[0]["invokerid"] == 0):
                continue

            # Look at all messages sent in server chat
            message = event[0]["msg"].lower()
            tokenized = message.split(" ")

            # Get command (the first string in msg)
            command = tokenized[0]

            # If the first token is the command !find or !get
            if "!find" == command or "!get" == command:
                data = {
                    "file": tokenized[1],
                    "user": event[0]["invokername"]
                }
                start_file_finder(data)

            elif "!roll" == command:
                data = {
                    "dice": tokenized[1],
                    "user": event[0]["invokername"]
                }
                roll_dice(data)

            elif "!say" == command:
                person = tokenized[1]
                say(person)

            elif "!cd" == command:
                data = {
                    "time": tokenized[1],
                    "user": event[0]["invokername"]
                }
                countdown(data)
            elif "!ltc" == command:
                data = {
                    "tokens": tokenized[1:],
                    "user": event[0]["invokername"]
                }
                do_ltc(data)

if __name__ == "__main__":
    with ts3.query.TS3ServerConnection(URI) as server_connection:
        tsconn = tsc(server_connection)
        start()
