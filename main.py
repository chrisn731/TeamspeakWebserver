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

sayings = {}

for (dirpath, dirnames, filenames) in os.walk(SAYINGS_DIR):
    for filename in filenames:
        with open(SAYINGS_DIR + filename, "r") as lines:
            person = filename.split(".")[0].lower()
            sayings_list = []
            for line in lines:
                sayings_list.append(line)
            sayings[person] = sayings_list


def send_global_msg(ts3conn, global_msg):
    ts3conn.exec_("gm", msg=global_msg)


def file_finder(ts3conn, item):
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
            ts3conn.exec_("gm", msg="I found some files of similar name:")
            for files in Found:
                ts3conn.exec_("gm", msg="{}".format(files))

    return tokens[len(tokens) - 1]

def say(ts3conn, person):

    if sayings[person]:
        lines = sayings[person]
        line = lines[random.randint(0,len(lines)-1)]
        ts3conn.exec_("gm", msg=line)
    else:
        ts3conn.exec_("gm", msg="Cannot find sayings for " + person)

def roll_dice(ts3conn, data):

    dice = data["dice"]
    user = data["user"]

    numbers = dice.split("d")
    if numbers != 2:
        send_global_msg(ts3conn, "Roll Usage: !roll {# of dice}d{# of sides on die}")
        return

    num_die = int(numbers[0])
    die_val = int(numbers[1])

    # Handle cases of funny people
    if num_die < 1:
        ts3conn.exec_("gm", msg="Throw atleast 1 die!")
        return

    if num_die > 1000:
        ts3conn.exec_("gm", msg="Too many dice!")
        return

    if die_val < 1:
        ts3conn.exec_("gm", msg="The universe has imploded!")
        return

    if die_val > 100:
        ts3conn.exec_("gm", msg="That die is too large!")
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

    ts3conn.exec_("gm", msg=output.format(user=user, dice=dice, total=total))

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

def countdown_get(ts3conn, data):
    timer = int(data["time"])
    user = data["user"]

    # Handle cases of funny people
    if timer < 0:
        ts3conn.exec_("gm", msg="Fuck you")
        return

    if timer > 1000:
        ts3conn.exec_("gm", msg="Fuck you")
        return

    while timer != 0:
        output = "{user}'s Timer: {timer}"
        ts3conn.exec_("gm", msg=output.format(user=user, timer=timer))
        time.sleep(1)
        timer -= 1

    if timer == 0:
        output = "{user}'s Timer Is Up"
        ts3conn.exec_("gm", msg=output.format(user=user))
        return

def start_file_finder(ts3conn, data):
    # Find the ID of the channel so we know where to look in the database
    file_to_find = data["file"]
    user = data["user"]

    channelid = file_finder(ts3conn, tokenized[1])
    # ChannelID > 0, so if < 0 the file wont exit
    if channelid == 0:
        ts3conn.exec_("gm", msg="I'm sorry, {}, but that file does not seem \
                        to exist.".format(user))
    elif channelid == 4:
        ts3conn.exec_("gm", msg="I'm sorry, {}, but you need to be more specific.\
                        Too many results returned".format(user))
    else:
        # Connect to the database and look through the channel_properties
        Channel_Name = database_search(int(channelid))
        if Channel_Name == "error":
            ts3conn.exec_("gm", msg="Sorry, {}, There was an issue acccessing \
                            the server database.".format(user))

        # Do something based on what the user wants
        elif "!find" in message:
            ts3conn.exec_("gm", msg="That file seems to be located in: {}".format(Channel_Name))
        else:
            pass


def start(ts3conn):
    ts3conn.exec_("servernotifyregister", event="textserver")
    #ts3conn.exec_("servernotifyregister", event="channel", id=7)
    while True:
        try:
            event = ts3conn.wait_for_event(timeout=60)
            ts3conn.send_keepalive()
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
            if "!find" == command or "!get" == command: #
                data = {
                        "file": tokenized[1],
                        "user": event[0]["invokername"]
                }
                start_file_finder(ts3conn, data)

            elif "!roll" == command:
                data = {
                    "dice": tokenized[1],
                    "user": event[0]["invokername"]
                }
                roll_dice(ts3conn, data)

            elif "!say" == command:
                person = tokenized[1]
                say(ts3conn, person)

            elif "!cd" == command:
                data = {
                    "time": tokenized[1],
                    "user": event[0]["invokername"]
                }
                countdown_get(ts3conn, data)

if __name__ == "__main__":
    with ts3.query.TS3ServerConnection(URI) as ts3conn:
        ts3conn.exec_("use", sid=SID)
        start(ts3conn)
