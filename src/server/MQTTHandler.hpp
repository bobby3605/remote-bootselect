#pragma once
#include "ConfigHandler.hpp"
#include "EventHandler.hpp"
#include "common.hpp"
#include <mosquitto.h>
#include <string>

void message_callback(mosquitto* mqtt, void* obj, const mosquitto_message* msg);

class MQTTHandler {
  public:
    MQTTHandler(EventHandler& eventHandler, ConfigHandler& configHandler, std::string const& host, uint16_t const& port,
                std::string const& username, std::string const& password);
    ~MQTTHandler();
    void upload_menuentries(MAC const& source, std::unordered_map<std::string, std::string> const& menuentries);
    ConfigHandler& configHandler;

  private:
    const std::string mqtt_topic = "remote_bootselect";
    const std::string discovery_topic = "homeassistant/device/remote-bootselect/config";
    mosquitto* mqtt;
    int mqtt_socket = -1;
    int timer_fd = -1;
    void process_socket(uint32_t events);
    std::function<void(uint32_t events)> handler;
    void process_timer(uint32_t events);
    std::function<void(uint32_t events)> timerHandler;
};
