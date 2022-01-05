fake_clients = {}
base = "2021-02-13 02:16:20.171604|INFO    |VirtualServerBase|1  |client connected 'A H'(id:105) from 47.16.220.208:57020"

template = "2022-{month}-{day} {hour}:{min}:{second}.000000|INFO    |VirtualServerBase|1  |client {action} '{name}'(id:{id})"

def change_name(name, index):
    t = list(name)
    t[index] = chr(ord(t[index]) + 1)
    return "".join(t)

def create_fake_clients():
    start = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    next_name = ""
    index = 0
    for i in range(500, 1000):
        if index >= len(start):
            index = 0
        next_name = change_name(start, index)
        fake_clients[i] = start
        print("Generating client: " + start + ": " + str(i))
        start = next_name
        index += 1



def create_log():
    with open("ts3server_2021-03-01__01_01_01.00000_1.log") as f:
        for i in range(500, 1000):
            name = fake_clients[i]


create_fake_clients()

