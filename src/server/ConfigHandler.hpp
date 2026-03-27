#pragma once
#include "EventHandler.hpp"
#include <string>

class MQTTHandler;

class ConfigHandler {
  public:
    ConfigHandler(EventHandler& eventHandler);
    ~ConfigHandler();
    void process_socket(uint32_t events);
    void process_config(std::istream& config);
    MQTTHandler* mqttHandler = nullptr;

  private:
    int config_socket = -1;
    void create_socket(std::string const& path);
    std::function<void(uint32_t)> handler;
};
