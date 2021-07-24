#!/usr/bin/python3

# Clientlist printer for the 'CNap's, Buddies and Friends' server.
# To be used in command line only.
# Usage: $ python clientlist.py [ARGS]
# ARGS: NONE = Simple client list
#       -i   = client list with more technical details of each client

import ts3
from ts3.definitions import TextMessageTargetMode
import sys
import sqlite3
from datetime import datetime


def print_datetime():
    now = datetime.now()
    now = now.strftime("%d/%m/%Y %H:%M:%S")
    print("The current time is: {}".format(now))


"""
build_channel_and_client_list - return array of dictionaries that map channels to clients

Builds and returns an array of dictionaries based on currently connected clients.
Each dictionary within the array contains the fields:
    "cid" - Channel ID
    "channel_name" - Name of the channel
    "users" - array of client nicknames
    "total_clients" - number of clients connected to the channel
"""
def build_channel_and_client_list(ts3conn):
    client_list = ts3conn.query("clientlist").all()
    channel_list = ts3conn.query("channellist").all()

    # Remove serveradmin from clientlist
    client_list[:] = [client
            for client in client_list if client["client_type"] != "1"
    ]

    if len(client_list) == 0:
        return None

    active_channels = []
    for channel in channel_list:
        channel_id = channel["cid"]
        client_count = channel["total_clients"]

        if client_count != "0":
                active_channels.append(channel)

    for channel in active_channels:
        channel["users"] = [
            client["client_nickname"] for client in client_list
                                        if client["cid"] == channel["cid"]
        ]
        if channel["users"] is not None:
            channel["users"].sort()

    # Sort the channels by how many users are in them
    return sorted(active_channels, key = lambda c: c["total_clients"], reverse=True)


# A pretty client list. Lists name of channels clients are in.
# Get clients -> lookup channelnames -> Sort -> Print clientlist
def pretty_client_list(ts3conn):
    channel_sorted = build_channel_and_client_list(ts3conn)
    print("Clients Connected ({}):".format(len(channel_sorted)))
    if channel_sorted is None:
        print("\tNo clients currently connected. :(")

    # Print channels and clients
    for channel in channel_sorted:
        if len(channel["users"]) != 0:
            print("\t{}:".format(channel["channel_name"]))
            for client in channel["users"]:
                print("\t\t{}".format(client))

"""
client_list_for_webserver - Function used by Go webserver.

Prints a newline seperated list of channels with their contained clients
Each name is tab ('\t') seperated to make parsing of the strings easier
within Go code. This decision was also made because Teamspeak client names
allow for any and all characters besides control characters. ('\n', '\t', etc).
Thus new lines are used to seperate different channels, and tab characters are
used to split up clients within the channel.
"""
def client_list_for_webserver(ts3conn):
    channel_client_list = build_channel_and_client_list(ts3conn)
    for channel in channel_client_list:
        if len(channel["users"]) != 0:
            print("{}\t".format(channel["channel_name"]), end='')
            for client in channel["users"]:
                print("{}\t".format(client), end='')
            print("")


# List clients with technical details
def list_clients(ts3conn):
        clients = ts3conn.query("clientlist").all()
        print("Clients Connected ({}):".format(len(clients)))
        for client in clients:
            print(client)

URI = "telnet://serveradmin:0e40SwRo@192.168.1.34:10011"
SID = 1

# Instantiate a connection to the TS3 Query Client, and then parse
# the arguments given in the command line.
def main():
    with ts3.query.TS3ServerConnection(URI) as ts3conn:
        ts3conn.exec_("use", sid=SID)

        if len(sys.argv) - 1 == 0:
            print_datetime()
            pretty_client_list(ts3conn)
        elif sys.argv[1] == "-i":
            print_datetime()
            list_clients(ts3conn)
        elif sys.argv[1] == "-g":
            client_list_for_webserver(ts3conn)
            pass
        else:
            print("Usage: python clientlist.py [-i/NO ARG]\n" +
            "NO ARG: A human readable client list.\n" +
            "-i: A client list with technical details.")

if __name__ == "__main__":
    main()
