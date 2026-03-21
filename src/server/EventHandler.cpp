#include "EventHandler.hpp"
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

EventHandler::EventHandler() {
    epfd = epoll_create1(0);
    if (epfd == -1) {
        std::cout << "warning: epoll_create failed" << strerror(errno) << std::endl;
    }
}

EventHandler::~EventHandler() {
    if (epfd != -1) {
        close(epfd);
    }
}

void EventHandler::register_socket(int socket, std::function<void(void)>& f) {
    epoll_event event;
    event.events = EPOLLIN;
    event.data.ptr = &f;
    epoll_ctl(epfd, EPOLL_CTL_ADD, socket, &event);
}

void EventHandler::handle_events() {
    epoll_event event;
    while (true) {
        int event_count = epoll_wait(epfd, &event, 1, -1);
        if (event_count == 1) {
            (*reinterpret_cast<std::function<void(void)>*>(event.data.ptr))();
        } else {
            std::cout << "warning: epoll_wait returned unexpected count: " << event_count << std::endl;
        }
    }
}
