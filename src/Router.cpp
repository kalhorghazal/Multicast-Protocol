#include "../include/Router.h"

using namespace std;

int main(int argc, char* argv[]) {
    Router router(argv[1]);
    router.start();
    return 0;
}

Router::Router(string listen_port)
: listen_port(listen_port) {
    string pipe_path = string(PIPE_ROOT_PATH) + string(listen_port);
    unlink(pipe_path.c_str());
	mkfifo(pipe_path.c_str(), READ_WRITE);
}

void Router::start() {
    fd_set read_fds, copy_fds;
    FD_ZERO(&copy_fds);
    FD_SET(STDIN, &copy_fds);
    int max_fd = STDIN;
    int activity;
    char received_buffer[MAX_MESSAGE_SIZE] = {0};

    printf("Router is starting ...\n");
    while (true) {
        string router_pipe_read = string(PIPE_ROOT_PATH) + string(listen_port);
        int router_pipe_read_fd = open(router_pipe_read.c_str(), O_RDWR);
        FD_SET(router_pipe_read_fd, &copy_fds);
        max_fd = router_pipe_read_fd;

        map<int, string> clients_fds = add_clients_to_set(copy_fds, max_fd);
        map<int, string> groupservers_fds = add_groupservers_to_set(copy_fds, max_fd);
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

                // Connection message
                else if (fd == router_pipe_read_fd) {
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    handle_connection_message(string(received_buffer));
                }

                // Router message
                else if (routers_fds.find(fd) != routers_fds.end()) {
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    cout << "received router message: " << received_buffer << endl;
                }

                // Client message
                else if (clients_fds.find(fd) != clients_fds.end()){
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    handle_client_message(string(received_buffer));
                }

                // Groupserver message
                else if (groupservers_fds.find(fd) != groupservers_fds.end()){
                    read(fd, received_buffer, MAX_MESSAGE_SIZE);
                    handle_groupserver_message(string(received_buffer));
                }
            }
        }

        close(router_pipe_read_fd);
        close_others_fds(clients_fds);
        close_others_fds(groupservers_fds);
        close_others_fds(routers_fds);
        cout << "--------------- event ---------------" << endl;
    }
}

map<int, string> Router::add_routers_to_set(fd_set& fds, int& max_fd) {
    map<int, string> routers_fds;
    map<string, Link*>::iterator it;
    for (it = links.begin(); it != links.end(); it++) {
        int router_fd = open(it->second->get_read_pipe().c_str(), O_RDWR);
        routers_fds.insert({router_fd, it->first});
        FD_SET(router_fd, &fds);
        max_fd = (max_fd > router_fd) ? max_fd : router_fd;
    }

    return routers_fds;
}

map<int, string> Router::add_clients_to_set(fd_set& fds, int& max_fd) {
    map<int, string> clients_fds;
    map<string, pair<string, string>>::iterator it;
    for (it = clients_pipes.begin(); it != clients_pipes.end(); it++) {
        int client_fd = open(it->second.second.c_str(), O_RDWR);
        clients_fds.insert({client_fd, it->first});
        FD_SET(client_fd, &fds);
        max_fd = (max_fd > client_fd) ? max_fd : client_fd;
    }

    return clients_fds;
}

map<int, string> Router::add_groupservers_to_set(fd_set& fds, int& max_fd) {
    map<int, string> groupservers_fds;
    map<string, pair<string, string>>::iterator it;
    for (it = group_servers_pipes.begin(); it != group_servers_pipes.end(); it++) {
        int groupserver_fd = open(it->second.second.c_str(), O_RDWR);
        groupservers_fds.insert({groupserver_fd, it->first});
        FD_SET(groupserver_fd, &fds);
        max_fd = (max_fd > groupserver_fd) ? max_fd : groupserver_fd;
    }

    return groupservers_fds;
}

void Router::close_others_fds(map<int, string> others_fds){
    map<int, string>::iterator it;
    for (it = others_fds.begin(); it != others_fds.end(); it++)
        close(it->first);
}

void Router::handle_command(string command) {
    vector<string> command_parts = split(command, COMMAND_DELIMITER);

    if (command_parts.size() < 1)
        return;

    else if (command_parts[ARG0] == ROUTER_TO_ROUTER_CONNECT_CMD)
        handle_connect_router(command_parts[ARG1], command_parts[ARG2]);

    else if (command_parts[ARG0] == CHANGE_COST_CMD)
        handle_change_cost(command_parts[ARG1], command_parts[ARG2]);

    else if (command_parts[ARG0] == DISCONNECT_LINK_CMD)
        handle_disconnect(command_parts[ARG1]);

    else if (command_parts[ARG0] == SHOW_CMD)
        handle_show();

    else printf("Unknown command.\n");
}

void Router::handle_connect_router(string router_port, string link_name) {
    make_router_router_pipes(router_port);

    string read_pipe = string(PIPE_ROOT_PATH) + router_port + '-' + listen_port;
    string write_pipe = string(PIPE_ROOT_PATH) + listen_port + '-' + router_port;

    printf("Router with port %s connects.\n", router_port.c_str());

    Link* link = new Link(link_name, read_pipe, write_pipe);

    links.insert({link_name, link});

    string message = string(ROUTER_MESSAGE_PREFIX) + MESSAGE_DELIMITER;
    message += string("connect") + MESSAGE_DELIMITER;
    message += listen_port + MESSAGE_DELIMITER;
    message += link_name;

    string pipe_path = string(PIPE_ROOT_PATH) + router_port;
    int connection_pipe_fd = open(pipe_path.c_str(), O_RDWR);
    write(connection_pipe_fd, message.c_str(), strlen(message.c_str()) + 1);
    close(connection_pipe_fd);
}

void Router::handle_connection_message(string pipe_message) {
    vector<string> message_parts = split(pipe_message, MESSAGE_DELIMITER);

    if (message_parts[ARG0] == ROUTER_MESSAGE_PREFIX && message_parts[ARG1] == "connect")
        accept_router_connect(message_parts[ARG2], message_parts[ARG3]);

    if (message_parts[ARG0] == CLIENT_MESSAGE_PREFIX && message_parts[ARG1] == "connect")
        accept_client_connect(message_parts[ARG2]);

    if (message_parts[ARG0] == GROUPSERVER_MESSAGE_PREFIX && message_parts[ARG1] == "connect")
        accept_groupserver_connect(message_parts[ARG2]);
}

void Router::accept_router_connect(string router_port, string link_name) {
    string read_pipe = string(PIPE_ROOT_PATH) + router_port + '-' + listen_port;
    string write_pipe = string(PIPE_ROOT_PATH) + listen_port + '-' + router_port;

    printf("Router with port %s connects.\n", router_port.c_str());

    Link* link = new Link(link_name, read_pipe, write_pipe);

    links.insert({link_name, link});
}

void Router::make_router_router_pipes(string router_port) {
    string read_pipe = string(PIPE_ROOT_PATH) + router_port + '-' + listen_port;
    string write_pipe = string(PIPE_ROOT_PATH) + listen_port + '-' + router_port;
    unlink(read_pipe.c_str());
	mkfifo(read_pipe.c_str(), READ_WRITE);
    unlink(write_pipe.c_str());
	mkfifo(write_pipe.c_str(), READ_WRITE);
}

void Router::accept_client_connect(string client_ip) {
    printf("client with ip %s connects.\n", client_ip.c_str());

    pair<string, string> client_pipe = {(string(PIPE_ROOT_PATH) + ROUTER_PIPE + CLIENT_PIPE + PIPE_NAME_DELIMITER + client_ip + READ_PIPE),
            (string(PIPE_ROOT_PATH) + ROUTER_PIPE + CLIENT_PIPE + PIPE_NAME_DELIMITER + client_ip + WRITE_PIPE)};

    unlink(client_pipe.first.c_str());
	mkfifo(client_pipe.first.c_str(), READ_WRITE);
    unlink(client_pipe.second.c_str());
	mkfifo(client_pipe.second.c_str(), READ_WRITE);

    clients_pipes.insert({client_ip, client_pipe});
}

void Router::accept_groupserver_connect(string groupserver_ip) {
    printf("groupserver with ip %s connects.\n", groupserver_ip.c_str());

    pair<string, string> group_server_pipe = {(string(PIPE_ROOT_PATH) + ROUTER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + PIPE_NAME_DELIMITER + groupserver_ip + READ_PIPE),
            (string(PIPE_ROOT_PATH) + ROUTER_PIPE + GROUPSERVER_PIPE + PIPE_NAME_DELIMITER + groupserver_ip + WRITE_PIPE)};

    unlink(group_server_pipe.first.c_str());
	mkfifo(group_server_pipe.first.c_str(), READ_WRITE);
    unlink(group_server_pipe.second.c_str());
	mkfifo(group_server_pipe.second.c_str(), READ_WRITE);

    group_servers_pipes.insert({groupserver_ip, group_server_pipe});
}

void Router::handle_change_cost(string link_name, string cost) {
    Link* link = links.find(link_name)->second;
    string write_pipe = link->get_write_pipe();

    string message = string(ROUTER_MESSAGE_PREFIX) + MESSAGE_DELIMITER;
    message += string("change_cost") + MESSAGE_DELIMITER;
    message += link_name + MESSAGE_DELIMITER;
    message += cost;

    int write_pipe_fd = open(write_pipe.c_str(), O_RDWR);
    write(write_pipe_fd, message.c_str(), strlen(message.c_str()) + 1);
    close(write_pipe_fd);

    link->change_cost(stod(cost));
    printf("Cost of link named %s changed to %s.\n", link_name.c_str(), cost.c_str());
}

void Router::handle_disconnect(string link_name) {
    Link* link = links.find(link_name)->second;
    string write_pipe = link->get_write_pipe();

    string message = string(ROUTER_MESSAGE_PREFIX) + MESSAGE_DELIMITER;
    message += string("disconnect") + MESSAGE_DELIMITER;
    message += link_name;

    int write_pipe_fd = open(write_pipe.c_str(), O_RDWR);
    write(write_pipe_fd, message.c_str(), strlen(message.c_str()) + 1);
    close(write_pipe_fd);

    links.erase(link_name);
    printf("Link named %s diconnected.\n", link_name.c_str());
}

void Router::handle_show() {

}

void Router::handle_router_message(string router_message) {
    vector<string> message_parts = split(router_message, MESSAGE_DELIMITER);

    if (message_parts[ARG1] == "change_cost")
        accept_router_change_cost(message_parts[ARG2], message_parts[ARG3]);

    else if (message_parts[ARG1] == "disconnect")
        accept_router_disconnect(message_parts[ARG2]);
    
    else printf("Unknown message.\n");
}

void Router::accept_router_change_cost(string link_name, string cost) {
    Link* link = links.find(link_name)->second;
    link->change_cost(stod(cost));

    printf("Cost of link named %s changed to %s.\n", link_name.c_str(), cost.c_str());
}

void Router::accept_router_disconnect(string link_name) {
    links.erase(link_name);
    printf("Link named %s diconnected.\n", link_name.c_str());
}

void Router::handle_client_message(std::string client_message) {
    cout << "client message: " << client_message << endl;
}

void Router::handle_groupserver_message(std::string client_message) {
    cout << "groupserver message: " << client_message << endl;
}
