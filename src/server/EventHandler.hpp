#pragma once
#include <functional>

class EventHandler {
  public:
    EventHandler();
    ~EventHandler();
    void register_socket(int socket, std::function<void(void)>& f);
    void handle_events();

  private:
    int epfd;
};
