#pragma once
#include "EventHandler.hpp"
#include <string>

class ConfigHandler {
  public:
    ConfigHandler(EventHandler& eventHandler);
    ~ConfigHandler();
    void process_socket();
    void process_config(std::istream& config);

  private:
    int config_socket = -1;
    void create_socket(std::string const& path);
    std::function<void(void)> handler;
};
