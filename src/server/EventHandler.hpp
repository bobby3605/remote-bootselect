#pragma once
#include <functional>
#include <sys/epoll.h>

class EventHandler {
  public:
    EventHandler();
    ~EventHandler();
    void register_socket(int socket, std::function<void(uint32_t)>& f, uint32_t events = EPOLLIN);
    void handle_events();

  private:
    int epfd;
};
