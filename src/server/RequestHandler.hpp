#pragma once
#include "EventHandler.hpp"
#include "common.hpp"
#include <string>
#include <sys/socket.h>
#include <unordered_map>

class RequestHandler {
  public:
    RequestHandler(EventHandler& eventHandler, std::string const& interface);
    ~RequestHandler();

  private:
    void create_data_socket();
    int data_socket = -1;
    std::function<void(void)> handler;
    MAC hwaddr = {};
    int ifindex = -1;
    void get_if_info(std::string const& interface);
    void process_socket();
    void process_request();
    void process_menuentries(size_t const& bufsize);
    std::unordered_map<std::string, std::string> menuentries;
};
