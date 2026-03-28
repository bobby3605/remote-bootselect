#include "EventHandler.hpp"
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

EventHandler::EventHandler() {
    epfd = epoll_create1(0);
    if (epfd == -1) {
        std::cout << "error: epoll_create failed" << strerror(errno) << std::endl;
        exit(errno);
    }
}

EventHandler::~EventHandler() {
    if (epfd != -1) {
        close(epfd);
    }
}

void EventHandler::register_socket(int socket, std::function<void(uint32_t)>& f, uint32_t events) {
    epoll_event event;
    event.events = events;
    event.data.ptr = &f;
    int r = epoll_ctl(epfd, EPOLL_CTL_ADD, socket, &event);
    if (r == -1) {
        std::cout << "error: failed to add socket to epoll: " << strerror(errno) << std::endl;
        exit(errno);
    }
}

void EventHandler::handle_events() {
    epoll_event event;
    while (true) {
        int event_count = epoll_wait(epfd, &event, 1, -1);
        if (event_count == 1) {
            (*reinterpret_cast<std::function<void(uint32_t)>*>(event.data.ptr))(event.events);
        } else {
            std::cout << "warning: epoll_wait returned unexpected count: " << event_count << std::endl;
        }
    }
}
