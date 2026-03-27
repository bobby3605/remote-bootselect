#include "ConfigHandler.hpp"
#include "EventHandler.hpp"
#include "MQTTHandler.hpp"
#include "RequestHandler.hpp"
#include "common.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <unordered_map>

std::unordered_map<MAC, std::string> defaultEntries;

int main(int argc, char* argv[]) {
    EventHandler eventHandler;
    ConfigHandler configHandler(eventHandler);
    std::string ifname;
    std::string host;
    uint16_t port = -1;
    std::string username;
    std::string password;
    std::string configFile;
    for (int i = 0; i + 1 < argc; i++) {
        std::string const& arg = argv[i];
        if (arg.compare("-i") == 0) {
            ifname = std::string(argv[++i]);
        } else if (arg.compare("-c") == 0) {
            configFile = argv[++i];
        } else if (arg.compare("-host") == 0) {
            host = std::string(argv[++i]);
        } else if (arg.compare("-port") == 0) {
            port = std::stoi(argv[++i]);
        } else if (arg.compare("-user") == 0) {
            username = std::string(argv[++i]);
        } else if (arg.compare("-pass") == 0) {
            password = std::string(argv[++i]);
        }
    }

    if (ifname.size() == 0) {
        std::cout << "error: interface option missing" << std::endl;
    } else {
        MQTTHandler mqttHandler(eventHandler, configHandler, host, port, username, password);
        RequestHandler requestHandler(eventHandler, mqttHandler, ifname);

        configHandler.mqttHandler = &mqttHandler;

        if (configFile.size() > 0) {
            std::ifstream config(configFile, std::ios::in);
            if (config.is_open()) {
                configHandler.process_config(config);
                config.close();
            } else {
                std::cout << "warning: failed to open config file: " << configFile << std::endl;
            }
        }

        eventHandler.handle_events();
    }
}
