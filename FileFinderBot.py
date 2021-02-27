# C C C C C C C C C C C C

import ts3
import time
import sys
import os
import subprocess
import sqlite3
import random

SID = 1
URI = "telnet://serveradmin:IUy8lW5R@localhost:10011"

# rename this file to main plz

# Maybe we make a file for everyone's classic lines
evan_lines = []
evan_txt = open("evan.txt", "r")
for line in evan_txt:
    evan_lines.append(line)

def file_finder(ts3conn, item):
    # Create a found files
    Directory = ""
    Found = []
    for(dirpath, dirnames, filenames) in os.walk("C:\\Users\\jyuen\\Desktop\\Goods\\Programming\\python\\TS_Bot\\teamspeak3-server_win64\\files\\virtualserver_1\\"):
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

def evan(ts3conn):
    line = evan_lines[random.randint(0,len(evan_lines)-1)]
    ts3conn.exec_("gm", msg=line)

def roll_dice(ts3conn, data):

    dice = data["dice"]
    user = data["user"]

    numbers = dice.split("d")
    num_die = int(numbers[0])
    die_val = int(numbers[1])

    # Handle cases of funny people
    if num_die < 1:
        ts3conn.exec_("gm", msg="Throw atleast 1 die!")
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
    conn = sqlite3.connect('teamspeak3-server_win64\\ts3server.sqlitedb')
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


def start(ts3conn):
    ts3conn.exec_("servernotifyregister", event="textserver")
    while True:
        try:
            event = ts3conn.wait_for_event(timeout=60)
        except ts3.query.TS3TimeoutError:
            print("Timeout error, consider looking into keepalive log")
        else:
            # Look at all messages sent in server chat
            message = event[0]["msg"]
            tokenized = message.split(" ")

            # Get command (the first string in msg)
            command = tokenized[0]

            # If the first token is the command !find or !get
            if "!find" == command or "!get" == command: #
                # Find the ID of the channel so we know where to look in the database
                channelid = file_finder(ts3conn, tokenized[1])

                # ChannelID > 0, so if < 0 the file wont exit
                if channelid == 0:
                    ts3conn.exec_("gm", msg="I'm sorry, {}, but that file does not seem to exist.".format(event[0]["invokername"]))
                elif channelid == 4:
                    ts3conn.exec_("gm", msg="I'm sorry, {}, but you need to be more specific. Too many results returned".format(event[0]["invokername"]))
                else:

                # Connect to the database and look through the channel_properties
                    Channel_Name = database_search(int(channelid))
                    if Channel_Name == "error":
                        ts3conn.exec_("gm", msg="Sorry, {}, There was an issue acccessing the server database.".format(event[0]["invokername"]))

                # Do something based on what the user wants
                    elif "!find" in message:
                        ts3conn.exec_("gm", msg="That file seems to be located in: {}".format(Channel_Name))
                    else:
                        pass

            elif "!roll" == command:
                data = {
                    "dice": tokenized[1],
                    "user": event[0]["invokername"]
                }
                roll_dice(ts3conn, data)

            elif "!evan" == command:
                evan(ts3conn)

            # Don't think we need this
            #event[0]["msg"] = "" 

    return

if __name__ == "__main__":
    with ts3.query.TS3ServerConnection(URI) as ts3conn:
        ts3conn.exec_("use", sid=SID)
        start(ts3conn)
