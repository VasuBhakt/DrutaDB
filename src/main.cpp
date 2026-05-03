#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <map>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <netdb.h>
#include "parser_resp.hpp"
#include "aof.hpp"

int main(int argc, char **argv) {
    // Flush after every std::cout / std::cerr
    // These lines ensure that whenever you print something, 
    // it shows up immediately in the console without waiting 
    // for a buffer to fill up. Essential for debugging!
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::cout << "Replaying AOF..." << std::endl;
    replay_aof();
    std::cout << "AOF Replay complete!" << std::endl;
    
    // socket() creates an endpoint for communication.
    // AF_INET: Use IPv4 addresses.
    // SOCK_STREAM: Use TCP (reliable, ordered delivery).
    // 0: Use the default protocol for TCP.
    // What server_fd does: It tells the OS which socket you are talking about.
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // Since the tester restarts your program quite often, setting SO_REUSEADDR
    // ensures that we don't run into 'Address already in use' errors
    // If you restart your server quickly, the OS often keeps 
    // the port "busy" for a minute. This option lets you 
    // reclaim the port immediately.
    int reuse = 1;
    // this is becuase of the close(server_fd) statement to ensure immediate regain of control in case of restart
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        return 1;
    }

    // What server_addr does: It tells the OS where that socket should live (IP and Port).
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;  // IPv4
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Listen on localhost only
    server_addr.sin_port = htons(6379); // Redis port (6379). htons() ensures "Big Endian" byte order for the network.

    // bind() associates the socket with the specific IP and Port.
    if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port 6379\n";
        return 1;
    }

    // listen() tells the OS we are ready to receive connections.
    // connection_backlog = 5 means 5 pending connections can wait in queue.
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        return 1;
    }

    // watchlist for sockets
    std::vector<struct pollfd> poll_fds;

    // 1. Add the server_fd to the watchlist first
    // For new connections
    struct pollfd server_pfd;
    server_pfd.fd = server_fd;
    server_pfd.events = POLLIN; // POLLIN means data is ready to be read; events and revents are BITFIELDS
    poll_fds.push_back(server_pfd);

    std::map<int, RespParser> client_parsers;

    while(true) {
        // 2. Ask the OS to wait until SOMETHING happens
        // poll (list(pointer to vector), size, timeout_in_ms)
        int new_events = poll(poll_fds.data(), poll_fds.size(), -1);
        if(new_events < 0) {
            std::cerr<<"Poll failed\n";
            break;
        }
        // 3. Loop through our watchlist to see who signaled us
        for(size_t i=0;i<poll_fds.size();i++) {
            if(poll_fds[i].revents & POLLIN) { // does this fd have something new?
                if(poll_fds[i].fd == server_fd) {
                    // EVENT :  New connection
                    struct sockaddr_in client_addr;
                    int client_addr_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &client_addr_len);
                    if(client_fd >= 0) {
                        std::cout << "Client connected!\n";
                        // add NEW client to watchlist
                        struct pollfd client_pfd;
                        client_pfd.fd = client_fd;
                        client_pfd.events = POLLIN;
                        poll_fds.push_back(client_pfd);
                    }
                } else {
                    // EVENT : An existing client sent a message (PING)
                    int current_client_fd = poll_fds[i].fd;
                    char buffer[1024];
                    int bytes_recieved = recv(current_client_fd, buffer, sizeof(buffer), 0);
                    if(bytes_recieved <= 0) {
                        std::cout<<"Client disconnected\n";
                        client_parsers.erase(current_client_fd);
                        close(current_client_fd);
                        // poll_fds.erase(poll_fds.begin()+i); O(n) in worst time
                        // this is ABSOLUTE GENIUS!
                        poll_fds[i] = poll_fds.back();
                        poll_fds.pop_back();
                        i--;
                    } else {
                        client_parsers[current_client_fd].parse_and_execute(buffer, bytes_recieved, current_client_fd);
                        // send(current_client_fd, response, strlen(response), 0);
                    }
                }
            }
        }
    }
    close(server_fd);
    return 0;
}