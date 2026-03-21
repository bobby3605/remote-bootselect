#include "ConfigHandler.hpp"
#include "common.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <sys/epoll.h>
#include <sys/un.h>
#include <unistd.h>

ConfigHandler::ConfigHandler(EventHandler& eventHandler) {
    create_socket("/tmp/remote-bootselect.sock");
    if (config_socket != -1) {
        handler = std::bind(&ConfigHandler::process_socket, this);
        eventHandler.register_socket(config_socket, handler);
    }
}

ConfigHandler::~ConfigHandler() {
    if (config_socket != -1) {
        close(config_socket);
    }
}

void ConfigHandler::create_socket(std::string path) {
    config_socket = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (config_socket != -1) {
        sockaddr_un addr = {};
        addr.sun_family = AF_UNIX;
        std::memcpy(addr.sun_path, path.data(), path.size());
        addr.sun_path[path.size()] = '\0';
        // ensure that the socket no longer exists
        // linux doesn't automatically clean up unix sockets
        unlink(path.data());
        if (bind(config_socket, (sockaddr*)&addr, sizeof(addr)) == -1) {
            std::cout << "warning: failed to bind socket: " << path << std::endl;
        }
    } else {
        std::cout << "warning: failed to create config socket" << std::endl;
    }
}

void ConfigHandler::process_socket() {
    size_t bufsize = 0;
    if (ioctl(config_socket, FIONREAD, &bufsize) < 0) {
        std::cout << "failed to get buffer size for config socket: " << strerror(errno) << std::endl;
        std::exit(errno);
    }
    if (bufsize > 0) {
        std::string buffer;
        buffer.resize(bufsize);
        int config_size = read(config_socket, buffer.data(), bufsize);
        if (config_size == -1) {
            std::cout << "config recv failed: " << strerror(errno) << std::endl;
            std::exit(errno);
        }
        std::stringstream stream(buffer);
        process_config(stream);
    }
}

void ConfigHandler::process_config(std::istream& config) {
    MAC mac;
    std::string entry;
    entry.reserve(MAX_ENTRY_LENGTH);
    while (!config.eof()) {
        if (parse_mac(config, mac)) {
            std::getline(config, entry);
            if (config.fail()) {
                std::cout << "warning: configuration failure" << std::endl;
                return;
            } else {
                defaultEntries[mac] = entry;
            }
        }
    }
}
