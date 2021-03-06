#include "../include/GroupServer.h"

using namespace std;

int main(int argc, char* argv[]) {
    GroupServer group_server(argv[1], argv[2]);
    group_server.start();
    return 0;
}

GroupServer::GroupServer(string group_name, string server_ip)
: group_name(group_name)
, ip(DEFALAUT_IP)
, group_ip(DEFALAUT_IP)
, server_ip(server_ip) {
}

void GroupServer::start() {
    fd_set read_fds, copy_fds;
    FD_ZERO(&copy_fds);
    FD_SET(STDIN, &copy_fds);
    int max_fd = STDIN;
    int activity;
    char received_buffer[MAX_MESSAGE_SIZE] = {0};

    printf("Group server is starting ...\n");
    while (true) {
        map<int, string> routers_fds = add_routers_to_set(copy_fds, max_fd);

        // Add fds to set
        memcpy(&read_fds, &copy_fds, sizeof(copy_fds));

        // Select
        cout << "> ";
        fflush(STDIN);
        activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0)
            return;

        int ready_sockets = activity;
        for (int fd = 0; fd <= max_fd  &&  ready_sockets > 0; ++fd) {
            if (FD_ISSET(fd, &read_fds)) {
                memset(received_buffer, 0, sizeof received_buffer);

                // Command line input
                if (fd == 0) {
                    fgets(received_buffer, MAX_COMMAND_SIZE, stdin);
                    received_buffer[strlen(received_buffer) - 1] = '\0';
                    cout << "received command: " << received_buffer << endl;
                    handle_command(string(received_buffer));
                }

                // Router message
                else if (routers_fds.find(fd) != routers_fds.end()) {
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    cout << "received router message: " << received_buffer << endl;
                }

                // Pipe pipe message
                else {
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    cout << "received pipe message: " << received_buffer << endl;
                    handle_pip_message(string(received_buffer));
                }
            }
        }

        close_others_fds(routers_fds);
        cout << "--------------- event ---------------" << endl;
    }
}

map<int, string> GroupServer::add_routers_to_set(fd_set& fds, int& max_fd) {
    map<int, string> routers_fds;
    map<string, pair<string, string>>::iterator it;
    for (it = groupserver_to_routers_pipes.begin(); it != groupserver_to_routers_pipes.end(); it++) {
        int router_fd = open(it->second.first.c_str(), O_RDWR);
        routers_fds.insert({router_fd, it->first});
        FD_SET(router_fd, &fds);
        max_fd = (max_fd > router_fd) ? max_fd : router_fd;
    }

    return routers_fds;
}

void GroupServer::close_others_fds(map<int, string> others_fds) {
    map<int, string>::iterator it;
    for (it = others_fds.begin(); it != others_fds.end(); it++)
        close(it->first);
}

void GroupServer::handle_command(string command) {
    vector<string> command_parts = split(command, COMMAND_DELIMITER);

    if (command_parts.size() < 1)
        return;

    if (command_parts[ARG0] == SET_GROUP_IP_CMD)
        handle_set_group_ip(command_parts[ARG1]);

    else if (command_parts[ARG0] == SET_GROUPSERVER_IP_CMD)
        handle_set_groupserver_ip(command_parts[ARG1]);

    else if (command_parts[ARG0] == GROUPSERVER_TO_ROUTER_CONNECT_CMD)
        handle_connect_router(command_parts[ARG1]);

    else if (command_parts[ARG0] == GROUPSERVER_TO_SERVER_CONNECT_CMD)
        handle_connect_server();

    else printf("Unknown command.\n");
}

void GroupServer::handle_set_group_ip(string group_ip) {
    this->group_ip = group_ip;
    printf("Group IP changed successfuly\n");
}

void GroupServer::handle_set_groupserver_ip(string groupserver_ip) {
    this->ip = groupserver_ip;
    printf("Group server IP changed successfuly\n");
}

void GroupServer::handle_connect_router(string router_port) {
    // Send connection message to router.
    string router_pipe = PIPE_ROOT_PATH + string(router_port);
    int router_pipe_fd = open(router_pipe.c_str(), O_RDWR);
    string router_connect_message = string(GROUPSERVER_MESSAGE_PREFIX) + MESSAGE_DELIMITER
            + string("connect") + MESSAGE_DELIMITER + group_ip;
    write(router_pipe_fd, router_connect_message.c_str(), router_connect_message.size());
    close(router_pipe_fd);
    pair<string, string> groupserver_to_router_pipe = {(string(PIPE_ROOT_PATH) + ROUTER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + group_ip + READ_PIPE),
            (string(PIPE_ROOT_PATH) + ROUTER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + group_ip + WRITE_PIPE)};
    
    groupserver_to_routers_pipes.insert({router_port, groupserver_to_router_pipe});
}

void GroupServer::handle_connect_server() {
    // Send connection message to server.
    string server_pipe = PIPE_ROOT_PATH + string(server_ip) + READ_PIPE;
    int server_pipe_fd = open(server_pipe.c_str(), O_RDWR);
    string connect_message = string(GROUPSERVER_TO_SERVER_CONNECT_MSG) + MESSAGE_DELIMITER + group_name
            + MESSAGE_DELIMITER + group_ip;
    write(server_pipe_fd, connect_message.c_str(), connect_message.size());
    close(server_pipe_fd);

    groupserver_to_server_pipe = {(string(PIPE_ROOT_PATH) + SERVER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + group_name + READ_PIPE),
            (string(PIPE_ROOT_PATH) + SERVER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + group_name + WRITE_PIPE)};
}

void GroupServer::handle_pip_message(string pipe_message) {
   
}
