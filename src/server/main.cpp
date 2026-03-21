#include "ConfigHandler.hpp"
#include "EventHandler.hpp"
#include "RequestHandler.hpp"
#include "common.hpp"
#include <cstring>
#include <fstream>
#include <iostream>

std::unordered_map<MAC, std::string> defaultEntries;

int main(int argc, char* argv[]) {
    EventHandler eventHandler;
    ConfigHandler configHandler(eventHandler);
    std::string ifname;
    for (int i = 0; i + 1 < argc; i++) {
        if (std::strcmp("-i", argv[i]) == 0) {
            ifname = std::string(argv[++i]);
        } else if (std::strcmp("-c", argv[i]) == 0) {
            std::ifstream config(argv[++i], std::ios::in);
            if (config.is_open()) {
                configHandler.process_config(config);
                config.close();
            } else {
                std::cout << "failed to open config file: " << argv[i] << std::endl;
            }
        }
    }

    if (ifname.size() == 0) {
        std::cout << "error: interface option missing" << std::endl;
    } else {
        RequestHandler requestHandler(eventHandler, ifname);
        eventHandler.handle_events();
    }
}
