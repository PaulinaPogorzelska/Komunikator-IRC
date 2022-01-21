import socket
import threading

# Setup

SERVER_IP = "192.168.8.173"
MANAGER_PORT = 25500
INFO_PORT = 25501

# ----- NET PROTOCOL -----

# Manager packets
LOGIN_PACKET = b'\x01'
LOGOUT_PACKET = b'\x02'
CREATE_CHANNEL_PACKET = b'\x03'

# Info packets
LIST_CHANNELS_PACKET = b'\x01'
LIST_MEMBERS_ON_CHANNEL_PACKET = b'\x00'

# Channel packets
JOIN_CHANNEL_PACKET = b'\x01'
LEAVE_CHANNEL_PACKET = b'\x00'


# ----- NET PROTOCOL FUNCTIONS -----

def net_login(user_name):
    manager_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    manager_socket.connect((SERVER_IP, MANAGER_PORT))

    manager_socket.send(LOGIN_PACKET)
    manager_socket.send(user_name.encode('utf-8'))

    return manager_socket


def net_logout(user_name):
    manager_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    manager_socket.connect((SERVER_IP, MANAGER_PORT))

    manager_socket.send(LOGOUT_PACKET)
    manager_socket.send(user_name.encode('utf-8'))

    return manager_socket


def net_join_channel(user_name, channel):
    join_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    join_socket.connect((SERVER_IP, channel['join_port']))
    join_socket.send(JOIN_CHANNEL_PACKET)

    join_socket.send(user_name.encode('utf-8'))

    join_socket.close()


def net_leave_channel(user_name, channel):
    leave_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    leave_socket.connect((SERVER_IP, channel['join_port']))
    leave_socket.send(LEAVE_CHANNEL_PACKET)

    leave_socket.send(user_name.encode('utf-8'))

    leave_socket.close()


def net_create_channel(creator_name, channel_name):
    manager_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    manager_socket.connect((SERVER_IP, MANAGER_PORT))
    manager_socket.send(CREATE_CHANNEL_PACKET)

    creator_name = bytes(creator_name, 'utf-8')

    for i in range(0, 32 - len(creator_name)):
        creator_name += b'\x00'

    manager_socket.send(creator_name + channel_name.encode('utf-8'))

    curr_channel_join_port = manager_socket.recv(4)

    if curr_channel_join_port == b'\x00':
        print("Failed to create channel on server")

    manager_socket.close()

    join_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    join_socket.connect((SERVER_IP, int.from_bytes(curr_channel_join_port, byteorder='little')))
    join_socket.send(JOIN_CHANNEL_PACKET)

    join_socket.send(creator_name)

    join_socket.close()


def net_get_channels():
    info_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    info_socket.connect((SERVER_IP, INFO_PORT))

    info_socket.send(LIST_CHANNELS_PACKET)

    number_of_channels = info_socket.recv(4)

    number_of_channels = int.from_bytes(number_of_channels, byteorder='little')

    channels_list = []
    for i in range(number_of_channels):
        channel_info = info_socket.recv(40)
        chat_port = int.from_bytes(channel_info[0:4], byteorder='little')
        join_port = int.from_bytes(channel_info[4:8], byteorder='little')
        channel_name = channel_info[8:40].split(b'\x00')
        channel_name = channel_name[0].decode('utf-8')
        channels_list.append({'chat_port': chat_port, 'join_port': join_port, 'channel_name': channel_name})

    info_socket.close()

    return channels_list


def net_get_channel_members(channel):
    info_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    info_socket.connect((SERVER_IP, INFO_PORT))

    info_socket.send(LIST_MEMBERS_ON_CHANNEL_PACKET)

    info_socket.send(channel["channel_name"].encode("utf-8"))

    number_of_channels = info_socket.recv(4)
    number_of_channels = int.from_bytes(number_of_channels, byteorder='little')

    members_on_channel = []
    for i in range(number_of_channels):
        member = info_socket.recv(32)
        member = member.split(b'\x00')
        member = member[0].decode('utf-8')
        members_on_channel.append(member)

    info_socket.close()

    return members_on_channel


def net_send_message(channel, message, creator_name):
    send_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    send_socket.connect((SERVER_IP, channel['chat_port']))

    creator_name = bytes(creator_name, 'utf-8')

    for i in range(0, 32 - len(creator_name)):
        creator_name += b'\x00'

    message = bytes(message, 'utf-8')

    for i in range(0, 160 - len(message)):
        message += b'\x00'

    send_socket.send(creator_name + message)

    send_socket.close()


def net_get_message(channel_conn_socket):
    received_message = channel_conn_socket.recv(224)

    author = received_message[0:32].split(b'\x00')
    author = author[0].decode('utf-8')

    channel = received_message[32:64].split(b'\x00')
    channel = channel[0].decode('utf-8')

    message = received_message[64:224].split(b'\x00')
    message = message[0].decode('utf-8')

    return {'author': author, 'channel': channel, 'message': message}


# ----- Text Client functions -----

print_sync_lock = threading.Lock()
receiver_thread_kill_signal = False


def chat_listener(messages_conn_socket):
    while True:
        rec_message = net_get_message(messages_conn_socket)
        if rec_message == b'':
            continue
        if receiver_thread_kill_signal:
            return
        print_sync_lock.acquire()
        print(f"{rec_message['author']}: {rec_message['message']}")
        print_sync_lock.release()


if __name__ == '__main__':
    username = input("Type your username: ")

    messages_socket = net_login(username)
    print(f"Logged in as {username}")

    receiver_thread = threading.Thread(target=chat_listener, args=(messages_socket, ))
    receiver_thread.start()

    curr_channel = net_get_channels()[0]
    main_channel = net_get_channels()[0]

    net_join_channel(username, curr_channel)

    while True:
        command = input()
        if command == "/logout":
            net_leave_channel(username, curr_channel)
            net_logout(username)
            receiver_thread_kill_signal = True
            print(f"Logged out")
            break

        elif command.startswith("/join-channel"):
            tmp_channels = net_get_channels()
            ch_name_parts = command.split(' ')
            ch_name = ch_name_parts[1]

            req_channel = None
            for ch in tmp_channels:
                if ch_name == ch['channel_name']:
                    req_channel = ch
                    break

            if req_channel is None:
                print(f"Unable to find channel \"{ch_name}\"")
            else:
                net_join_channel(username, req_channel)
                net_leave_channel(username, main_channel)
                curr_channel = req_channel
                print(f"Joined channel \"{ch_name}\"")

        elif command == "/leave-channel":
            tmp_channels = net_get_channels()

            req_channel = tmp_channels[0]

            net_leave_channel(username, curr_channel)

            net_join_channel(username, req_channel)

            print(f"Left channel \"{curr_channel['channel_name']}\"")

            curr_channel = req_channel

        elif command.startswith("/create-channel"):
            ch_name_parts = command.split(' ')
            ch_name = ch_name_parts[1]

            net_create_channel(username, ch_name)

            net_leave_channel(username, main_channel)

            tmp_channels = net_get_channels()
            req_channel = None
            for ch in tmp_channels:
                if ch_name == ch['channel_name']:
                    req_channel = ch
                    break

            curr_channel = req_channel

            print(f"Created channel {ch_name}")

        elif command == "/get-available-channels":
            tmp_channels = net_get_channels()
            print(f"Available channels ({len(tmp_channels)}):")
            for ch in tmp_channels:
                print(f"{ch['channel_name']}")

        elif command.startswith("/get-channel-members"):
            tmp_channels = net_get_channels()
            ch_name_parts = command.split(' ')
            ch_name = ch_name_parts[1]

            req_channel = None
            for ch in tmp_channels:
                if ch_name == ch['channel_name']:
                    req_channel = ch
                    break

            if req_channel is None:
                print(f"Unable to find channel \"{ch_name}\"")
            else:
                ch_members = net_get_channel_members(req_channel)
                print(f"Channel \"{ch_name}\" has {len(ch_members)} :")
                for mbr in ch_members:
                    print(f"{mbr}")

        elif command.startswith("/say"):
            ch_name_parts = command.split(' ')
            mts = ch_name_parts[1:]

            message_to_send = ''
            for part in mts:
                message_to_send += part + ' '

            net_send_message(curr_channel, message_to_send, username)

        else:
            print("Incorrect command")
