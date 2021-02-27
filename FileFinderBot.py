import ts3
import time
import sys
import os
import subprocess
import sqlite3

SID = 1
URI = "REDACTED"

def FileFinder(ts3conn, item):
    # Create a found files
    Directory = ""
    Found = []
    for(dirpath, dirnames, filenames) in os.walk("S:\\TS Server 3-26-20\\files\\virtualserver_1\\"):
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

def DataBaseSearch(channelid):
    conn = sqlite3.connect('ts3server.sqlitedb')
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


def FindBotStart(ts3conn):
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

            # If the message contains these keywords begin work...
            if "!find" in message or "!get" in message:
                # Find the ID of the channel so we know where to look in the database
                channelid = FileFinder(ts3conn, tokenized[1])

                # ChannelID > 0, so if < 0 the file wont exit
                if channelid == 0:
                    ts3conn.exec_("gm", msg="I'm sorry, {}, but that file does not seem to exist.".format(event[0]["invokername"]))
                elif channelid == 4:
                    ts3conn.exec_("gm", msg="I'm sorry, {}, but you need to be more specific. Too many results returned".format(event[0]["invokername"]))
                else:

                # Connect to the database and look through the channel_properties
                    Channel_Name = DataBaseSearch(int(channelid))
                    if Channel_Name == "error":
                        ts3conn.exec_("gm", msg="Sorry, {}, There was an issue acccessing the server database.".format(event[0]["invokername"]))

                # Do something based on what the user wants
                    elif "!find" in message:
                        ts3conn.exec_("gm", msg="That file seems to be located in: {}".format(Channel_Name))
                    else:
                        pass

            event[0]["msg"] = ""

    return

if __name__ == "__main__":
    with ts3.query.TS3ServerConnection(URI) as ts3conn:
        ts3conn.exec_("use", sid=SID)
        FindBotStart(ts3conn)
