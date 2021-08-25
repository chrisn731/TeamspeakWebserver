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
from fuzzywuzzy import fuzz
from fuzzywuzzy import process

SID = 1
# Must be edited to suit your workstation.
URI = ""
FILE_DIR = ""
SAYINGS_DIR = "./sayings/"

# Teamspeak Connection Class
# A nice wrapper around some of the commonly used commands to stop from
# having to deal with the wierd ts3conn.exec_() parameters and such.
# Should also make the code look overall more coherent.
class tsc:
    def __init__(self, ts3conn):
        self.ts3conn = ts3conn
        self.ts3conn.exec_("use", sid=SID)

        # Channel id to channel name map.
        # Should ONLY be accessed using translate_cid_to_name()
        self.cid_to_name_map = {}

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

    def build_cid_to_name_map(self):
        channel_list = self.get_channellist()
        for chan in channel_list:
            self.cid_to_name_map[chan["cid"]] = chan["channel_name"]

    def translate_cid_to_name(self, cid):
        if len(self.cid_to_name_map) == 0:
            self.build_cid_to_name_map()
        return self.cid_to_name_map[cid]


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

def ff_iterate_files(topdir, to_find):
    possible_files = []
    for (top, dirs, fns) in os.walk(FILE_DIR + topdir):
        token_ratio_tup = process.extract(to_find, fns)
        for tr in token_ratio_tup:
            token = tr[0]
            ratio = tr[1]
            if (ratio > 70):
                possible_files.append(token)

    if len(possible_files) != 0:
        return {"channel_name": topdir, "files": possible_files}
    else:
        return None

def start_file_finder(data):
    invoker = data["user"]
    file_to_find = data["file"]

    possible_files = []
    for (dirpath, dirs, filenames) in os.walk(FILE_DIR):
        for dirp in dirs:
            # "internal" is a folder that holds no useful information
            if dirp == "internal":
                continue
            rv = ff_iterate_files(dirp, file_to_find)
            if rv:
                possible_files.append(rv)

    tsconn.global_msg("[B][COLOR=#C02F1D]##### POSSIBLE FILES #####[/COLOR][/B]")
    for d in possible_files:
        real_channelname = tsconn.translate_cid_to_name(d["channel_name"][8:])
        tsconn.global_msg("[COLOR=#1287A8]{}[/COLOR]".format(real_channelname))

        for file in d["files"]:
            tsconn.global_msg("\t[COLOR=#DA621E]{}[/COLOR]".format(file))

    tsconn.global_msg("[B][COLOR=#C02F1D]##### END OF FILE LIST #####[/COLOR][/B]")

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
