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


def print_header():
        now = datetime.now()
        now = now.strftime("%d/%m/%Y %H:%M:%S")
        print("The current time is: {}".format(now))


# A pretty client list. Lists name of channels clients are in.
# Get clients -> lookup channelnames -> Sort -> Print clientlist
def better_client_list(ts3conn):
    client_list = ts3conn.query("clientlist").all()
    channel_list = ts3conn.query("channellist").all()

    # Remove serveradmin from clientlist
    client_list[:] = [client
            for client in client_list if client["client_type"] != "1"
    ]

    print("Clients Connected ({}):".format(len(client_list)))

    # Return if no clients found.
    if len(client_list) == 0:
        print("\tNo clients currently connected. :(")
        return

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
    channel_sorted = sorted(active_channels,
                            key = lambda c: c["total_clients"],
                            reverse=True)

    # Print channels and clients
    for channel in channel_sorted:
        if len(channel["users"]) != 0:
            print("\t{}:".format(channel["channel_name"]))
            for client in channel["users"]:
                print("\t\t{}".format(client))

# List clients with technical details
def listclients(ts3conn):
        clients = ts3conn.query("clientlist").all()
        print("Clients Connected ({}):".format(len(clients)))
        for client in clients:
            print(client)

URI = "YOUR_TELNET_INFO_HERE"
SID = 1

# Instantiate a connection to the TS3 Query Client, and then parse
# the arguments given in the command line.
def main():
    with ts3.query.TS3ServerConnection(URI) as ts3conn:
        ts3conn.exec_("use", sid=SID)

        if len(sys.argv) - 1 == 0:
            print_header()
            Better_Client_List(ts3conn)
        elif sys.argv[1] == "-i":
            print_header()
            listclients(ts3conn)
        else:
            print("Usage: python clientlist.py [-i/NO ARG]\n" +
            "NO ARG: A human readable client list.\n" +
            "-i: A client list with technical details.")

if __name__ == "__main__":
    main()
