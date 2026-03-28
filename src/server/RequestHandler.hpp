#pragma once
#include "EventHandler.hpp"
#include "MQTTHandler.hpp"
#include "common.hpp"
#include <string>
#include <sys/socket.h>

class RequestHandler {
  public:
    RequestHandler(EventHandler& eventHandler, MQTTHandler& mqttHandler, std::string const& interface);
    ~RequestHandler();

  private:
    void create_data_socket();
    int data_socket = -1;
    std::function<void(uint32_t)> handler;
    MAC hwaddr = {};
    int ifindex = -1;
    void get_if_info(std::string const& interface);
    void process_socket(uint32_t events);
    void process_request(std::vector<unsigned char> const& frame);
    void process_menuentries(std::vector<unsigned char> const& frame);
    MQTTHandler& mqttHandler;
};
