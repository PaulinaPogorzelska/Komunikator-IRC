#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <mutex>
#include <unistd.h>
#include <fcntl.h>

#define SERVER_IP "192.168.8.173"
#define MANAGER_PORT 25500
#define CHANNEL_INFO_PORT 25501

//Cosmetic colors
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

//Structs for network protocol
struct nick_t {
  char data[32] = {0};
};

struct message_t {
  char sender[32] = {0};
  char data[160] = {0};
};

struct full_message_t {
  char sender[32] = {0};
  char channel_name[32] = {0};
  char data[160] = {0};
};

struct create_channel_t {
  char creator[32] = {0};
  char channel_name[32] = {0};
};

struct channel_name_t {
  char channel_name[32] = {0};
};

struct channel_t {
  int chat_port = 0;
  int join_port = 0;
  char channel_name[32] = {0};
};

struct join_leave_t {
  bool join = true; //true if you want to join channel, false if you want to leave
};

struct channel_info_request_type_t {
  bool full = true; //true if you want all channels on server, false if you want members of specific channel
};

//Struct to identify what user wants to do
/*  1 - user wants to login
    2 - user wants to logout
    3 - user wants to create a new channel
*/
struct action_t {
  uint8_t action = 0;
};

//Server internal structs
struct channel_internal_t {
  std::string name;
  std::vector<std::string> memberes;
  int chat_port;
  int join_port;
};

struct logged_member_internal_t {
  int member_socket;
  std::string name;
};

//Globals
std::string MAIN_CHANNEL_NAME = "Main_Channel";

//Vector with current channels
std::vector<channel_internal_t> channels;

//Vector with currently logged memebers
std::vector<struct logged_member_internal_t> currently_logged;

//Locks for threads synchronization
std::mutex channel_list_edit_lock;
std::mutex member_list_edit_lock;

//Counter for unique ports
int next_channel_port = MANAGER_PORT + 2;

void channel_manager();
void channel_info_handler();
int create_channel(std::string channel_name, std::string creator);
void handle_channel(std::string channel_name, int channel_socket, int join_port);
void join_channel_handler(std::string channel_name, int join_socket);


void channel_manager() {
  int manager_socket;
  if ((manager_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    std::cout << COLOR_RED << "Error creating manager socket" << COLOR_RESET << "\n\r";
    exit(-1);
  }

  //Manager address
  struct sockaddr_in manager_address;
  //Manager adress setup
  manager_address.sin_family = AF_INET;
  manager_address.sin_addr.s_addr = inet_addr(SERVER_IP);
  manager_address.sin_port = htons(MANAGER_PORT);

  if (bind(manager_socket, (struct sockaddr*)&manager_address, sizeof(manager_address)) == -1)
  {
    std::cout << COLOR_RED << "Error while binding to manager socket\n Incorrect IP adress ?" << COLOR_RESET << "\n\r";
    exit(-1);
  }

  if (listen(manager_socket,128) == -1)
  {
    std::cout << COLOR_RED << "Error while listening to manager socket\n Incorrect IP adress ?" << COLOR_RESET << "\n\r";
    exit(-1);
  }
  
  std::cout << COLOR_YELLOW << "Manager port set to " << MANAGER_PORT << COLOR_RESET << "\n\r";

  create_channel(MAIN_CHANNEL_NAME, "Server");

  while(true) {
    int user_socket = accept(manager_socket, NULL, NULL);

    struct action_t user_action;
    recv(user_socket, &user_action, sizeof(user_action), 0);

    switch (user_action.action) {
      case 1: {
        struct nick_t user_nick;
        recv(user_socket, &user_nick, sizeof(nick_t), 0);

        while (member_list_edit_lock.try_lock()) {}

        struct logged_member_internal_t new_member;
        new_member.name = user_nick.data;
        new_member.member_socket = user_socket;
        currently_logged.push_back(new_member);

        member_list_edit_lock.unlock();

        std::cout << COLOR_CYAN << user_nick.data << " logged in" << COLOR_RESET << "\n\r";

        break;
      }
      case 2: {
        struct nick_t user_nick;
        recv(user_socket, &user_nick, sizeof(nick_t), 0);

        while (member_list_edit_lock.try_lock()) {}

        std::vector<struct logged_member_internal_t> tmp_members;

        for (int i = 0; i < currently_logged.size(); i++) {
          if (currently_logged[i].name != user_nick.data) {
            tmp_members.push_back(currently_logged[i]);
          }
        }

        currently_logged = tmp_members;

        member_list_edit_lock.unlock();

        std::cout << COLOR_CYAN << user_nick.data << " logged out" << COLOR_RESET << "\n\r";

        break;
      }
      case 3: {
        struct create_channel_t create_channel_task;
        recv(user_socket, &create_channel_task, sizeof(create_channel_task), 0);

        int join_port = create_channel(create_channel_task.channel_name, create_channel_task.creator);
        
        send(user_socket, &join_port, sizeof(int), 0);

        close(user_socket);

        break;
      }
      default: {
        std::cout << COLOR_RED << "Unsupported action" << COLOR_RESET << "\n\r";
        break;
      }
    }

  }
  
}

void channel_info_handler() {
  int info_socket;
  if ((info_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    std::cout << COLOR_RED << "Error creating channel info socket" << COLOR_RESET << "\n\r";
    exit(-1);
  }

  //Channel info address
  struct sockaddr_in info_address;
  //Channel adress setup
  info_address.sin_family = AF_INET;
  info_address.sin_addr.s_addr = inet_addr(SERVER_IP);
  info_address.sin_port = htons(CHANNEL_INFO_PORT);

  if (bind(info_socket, (struct sockaddr*)&info_address, sizeof(info_address)) == -1)
  {
    std::cout << COLOR_RED << "Error while binding to info socket\n Incorrect IP adress ?" << COLOR_RESET << "\n\r";
    exit(-1);
  }

  if (listen(info_socket,128) == -1)
  {
    std::cout << COLOR_RED << "Error while listening to info socket\n Incorrect IP adress ?" << COLOR_RESET << "\n\r";
    exit(-1);
  }
  
  std::cout << COLOR_YELLOW << "Info port set to " << CHANNEL_INFO_PORT << COLOR_RESET << "\n\r";

  while(true) {
    int user_socket = accept(info_socket, NULL, NULL);

    struct channel_info_request_type_t req_type;
    recv(user_socket, &req_type, sizeof(struct channel_info_request_type_t), 0);

    if (req_type.full) {
      while (channel_list_edit_lock.try_lock()) {}

      int number_of_channels = channels.size();

      send(user_socket, &number_of_channels, sizeof(int), 0);

      for (int i = 0; i < number_of_channels; i++) {
        struct channel_t tmp;
          for (int k = 0; k < channels[i].name.size(); k++) {
            tmp.channel_name[k] = channels[i].name[k];
          }
        tmp.chat_port = channels[i].chat_port;
        tmp.join_port = channels[i].join_port;

        send(user_socket, &tmp, sizeof(channel_t), 0);
      }

      channel_list_edit_lock.unlock();
    } else {
      
      struct channel_name_t requested_channel;
      recv(user_socket, &requested_channel, sizeof(channel_name_t), 0);

      while (channel_list_edit_lock.try_lock()) {}

      for (int i = 0; i < channels.size(); i++ ) {
        if (requested_channel.channel_name == channels[i].name) {
          int number_of_members = channels[i].memberes.size();

          send(user_socket, &number_of_members, sizeof(int), 0);

          for (int j = 0; j < number_of_members; j++) {
            struct nick_t tmp;

            for (int k = 0; k < channels[i].memberes[j].size(); k++) {
              tmp.data[k] = channels[i].memberes[j][k];
            }

            send(user_socket, &tmp, sizeof(nick_t), 0);

          }
          break; 
        }
      }

      channel_list_edit_lock.unlock(); 
    }

    close(user_socket);
  }
}

int create_channel(std::string channel_name, std::string creator) {
  int channel_socket;
  if ((channel_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    std::cout << COLOR_RED << "Error while creating socket for " << channel_name << COLOR_RESET << "\n\r";
    return 0;
  }

  //Channel adress setup
  struct sockaddr_in channel_address;

  channel_address.sin_family = AF_INET;
  channel_address.sin_addr.s_addr = inet_addr(SERVER_IP);
  channel_address.sin_port = htons(next_channel_port);

  if (bind(channel_socket, (struct sockaddr*)&channel_address, sizeof(channel_address)) == -1)
  {
    std::cout << COLOR_RED << "Error while binding to " << channel_name << " socket" << COLOR_RESET << "\n\r";
    return 0;
  }

  if (listen(channel_socket,128) == -1)
  {
    std::cout << COLOR_RED << "Error while listening to " << channel_name << "socket" << COLOR_RESET << "\n\r";
    return 0;
  }

  std::cout << COLOR_CYAN << channel_name << " port set to " << next_channel_port << COLOR_RESET << "\n\r";

  //If channel list is being edited than wait
  while (channel_list_edit_lock.try_lock()) {}

  channel_internal_t tmp;
  tmp.name = channel_name;
  tmp.chat_port = next_channel_port;
  tmp.join_port = next_channel_port + 1;
  channels.push_back(tmp);

  channel_list_edit_lock.unlock();

  std::thread new_channel_thread(handle_channel, channel_name, channel_socket, next_channel_port + 1);

  next_channel_port += 2;

  std::cout << COLOR_GREEN << "User " << creator << " created channel " << channel_name << COLOR_RESET << "\n\r";

  new_channel_thread.detach();

  return tmp.join_port;
}

void handle_channel(std::string channel_name, int channel_socket, int join_port) {

  //Join socket setup
  int join_socket;
  if ((join_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    std::cout << COLOR_RED << "Error while creating join socket for " << channel_name << COLOR_RESET << "\n\r";
    return;
  }

  //Channel adress setup
  struct sockaddr_in join_address;

  join_address.sin_family = AF_INET;
  join_address.sin_addr.s_addr = inet_addr(SERVER_IP);
  join_address.sin_port = htons(join_port);

  if (bind(join_socket, (struct sockaddr*)&join_address, sizeof(join_address)) == -1)
  {
    std::cout << COLOR_RED << "Error while binding to " << channel_name << " join socket" << COLOR_RESET << "\n\r";
    return;
  }

  if (listen(join_socket,128) == -1)
  {
    std::cout << COLOR_RED << "Error while listening to " << channel_name << " join socket" << COLOR_RESET << "\n\r";
    return;
  }

  std::cout << COLOR_CYAN << channel_name << " join port set to " << join_port << COLOR_RESET << "\n\r";

  int connected_client = accept(join_socket, NULL, NULL);

  //First member join
  struct join_leave_t creator_join;
  recv(connected_client,&creator_join, sizeof(join_leave_t), 0);

  struct nick_t channel_creator;
  recv(connected_client,&channel_creator, sizeof(nick_t), 0);

  std::cout << COLOR_CYAN << channel_creator.data << " joined channel " << channel_name << COLOR_RESET << "\n\r";

  //If channel list is being edited than wait
  while (channel_list_edit_lock.try_lock()) {}

  for (int i = 0; i < channels.size(); i++) {
    if (channels[i].name == channel_name) {
      channels[i].memberes.push_back(channel_creator.data);
    }
  }

  channel_list_edit_lock.unlock();

  close(connected_client);

  std::thread channel_joiner_thread(join_channel_handler, channel_name, join_socket);

  //Set socket to be non blocking
  fcntl(channel_socket, F_SETFL, O_NONBLOCK);

  while (true) {
    //Channel removal
    for (int i = 0; i < channels.size(); i++) {
      if (channel_name == MAIN_CHANNEL_NAME) continue;
      if (channels[i].name == channel_name) {
        if (channels[i].memberes.size() == 0) {
          std::cout << COLOR_MAGENTA << "Deleting channel " << channels[i].name << " due to being empty" << COLOR_RESET << "\n\r";
          //If channel list is being edited than wait
          while (channel_list_edit_lock.try_lock()) {}

          std::vector<channel_internal_t> tmp_channels;
          for (int j = 0; j < channels.size(); j++) {
            if ((channels[j].memberes.size() != 0) || (channels[j].name == MAIN_CHANNEL_NAME))
              tmp_channels.push_back(channels[j]);
          }
          channels = tmp_channels;

          channel_list_edit_lock.unlock();

          close(join_socket);
          channel_joiner_thread.join();

          std::cout << COLOR_GREEN << "Channel " << channels[i].name << " deleted" << COLOR_RESET << "\n\r";

          return;
        }
      }
    }

    //Chat handling
    int connected_member = accept(channel_socket, NULL, NULL);

    //If there is no connection than start next iteration
    if (connected_member == -1) continue;

    struct message_t recieved_msg;
    recv(connected_member, &recieved_msg, sizeof(message_t), 0);

    //Message repack to full data format
    struct full_message_t full_message;
    for (int i = 0; i < (sizeof(recieved_msg.sender)/sizeof(char)); i++) {
      full_message.sender[i] = recieved_msg.sender[i];
    }
    for (int i = 0; i < channel_name.size(); i++) {
      full_message.channel_name[i] = channel_name[i];
    }
    for (int i = 0; i < (sizeof(recieved_msg.data)/sizeof(char)); i++) {
      full_message.data[i] = recieved_msg.data[i];
    }
    
    std::cout << "(" << full_message.channel_name << ") " << full_message.sender << ": " << full_message.data << "\n\r";

    while (channel_list_edit_lock.try_lock()) {}
    while (member_list_edit_lock.try_lock()) {}

    for (int i = 0; i < channels.size(); i++) {
      if (channels[i].name == channel_name) {
        for (int j = 0; j< channels[i].memberes.size(); j++) {
          for (int k = 0; k < currently_logged.size(); k++) {
            if (channels[i].memberes[j] == currently_logged[k].name) {
              send(currently_logged[k].member_socket, &full_message, sizeof(full_message_t), 0);
            }
          }
        }
        break;
      }
    }

    member_list_edit_lock.unlock();
    channel_list_edit_lock.unlock();
  }
}

void join_channel_handler(std::string channel_name, int join_socket) {
  //Set socket to be non blocking
  fcntl(join_socket, F_SETFL, O_NONBLOCK);

  bool found;
  while (true) {
    found = false;
    while (channel_list_edit_lock.try_lock()) {}

    for (int i = 0; i < channels.size(); i++) {
      if (channels[i].name == channel_name) {
        found = true;
      }
    }

    channel_list_edit_lock.unlock();
    
    if (!found) {
      std::cout << COLOR_CYAN << "Channel " << channel_name << " deleted, removing join thread" << COLOR_RESET << "\n\r";
      return;
    }

    int connected_client = accept(join_socket, NULL, NULL);

    //If there is no connection than start next iteration
    if (connected_client == -1) continue;

    struct join_leave_t member_join_leave;
    recv(connected_client,&member_join_leave, sizeof(join_leave_t), 0);

    struct nick_t new_member;
    recv(connected_client,&new_member, sizeof(nick_t), 0);
    

    if (member_join_leave.join) {
      //If channel list is being edited than wait
      while (channel_list_edit_lock.try_lock()) {}

      for (int i = 0; i < channels.size(); i++) {
        if (channels[i].name == channel_name) {
          channels[i].memberes.push_back(new_member.data);
        }
      }

      channel_list_edit_lock.unlock();

      std::cout << COLOR_CYAN << new_member.data << " joined channel " << channel_name << COLOR_RESET << "\n\r";
      
    } else {
      //If channel list is being edited than wait
      while (channel_list_edit_lock.try_lock()) {}

      for (int i = 0; i < channels.size(); i++) {
        if (channels[i].name == channel_name) {
          std::vector<std::string> tmp_members;
          for (int j=0; j<channels[i].memberes.size(); j++) {
            if (channels[i].memberes[j] != new_member.data){
              tmp_members.push_back(channels[i].memberes[j]);
            }
          }
          channels[i].memberes = tmp_members;
        }
      }

      channel_list_edit_lock.unlock();

      std::cout << COLOR_CYAN << new_member.data << " left channel " << channel_name << COLOR_RESET << "\n\r";

    }

    close(connected_client);
  }
}

int main() {
  std::thread manager_thread(channel_manager);
  std::thread channel_info_thread(channel_info_handler);

  std::cout << COLOR_YELLOW << "Server adress set to " << SERVER_IP << COLOR_RESET << "\n\r";

  //Block + commands so server doesn't shuts down
  while (true){
    std::string command;
    std::getline(std::cin, command, '\n');
    if (command == "list channels") {
      std::cout << COLOR_CYAN << "Channels on server:" << COLOR_RESET << "\n\r";
      for (int i = 0; i < channels.size(); i++) {
        std::cout << channels[i].name << "\n\r";
      }
    } else if (command == "list channels members") {
      for (int i = 0; i < channels.size(); i++) {
        std::cout << COLOR_CYAN << "On channel " << COLOR_RESET << channels[i].name << COLOR_CYAN << " there are " << COLOR_RESET << channels[i].memberes.size() << COLOR_CYAN << " members:" << COLOR_RESET << "\n\r";
        for (int j = 0; j < channels[i].memberes.size(); j++) {
          std::cout<< channels[i].memberes[j] << "\n\r";
        }
      }
    } else if (command == "list logged") {
      std::cout << COLOR_CYAN << "Currently logged:" << COLOR_RESET << "\n\r";
      for (int i = 0; i < currently_logged.size(); i++) {
        std::cout << currently_logged[i].name << " on socket " << currently_logged[i].member_socket << "\n\r";
      }

    } else {
      std::cout << COLOR_RED << "Incorrect command" << COLOR_RESET << "\n\r";
    }
  };
  
  return 0;
}
