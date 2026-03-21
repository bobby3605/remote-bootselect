#pragma once
#include "EventHandler.hpp"
#include <string>
#include <sys/socket.h>

class RequestHandler {
  public:
    RequestHandler(EventHandler& eventHandler, std::string const& interface);

  private:
    void create_data_socket();
    int data_socket;
    std::function<void(void)> handler;
    sockaddr hwaddr = {};
    int ifindex = -1;
    void get_if_info(std::string const& interface);
    void process_socket();
};
