#include "MQTTHandler.hpp"
#include "mosquitto.h"
#include "src/server/ConfigHandler.hpp"
#include <iostream>
#include <json.hpp>
#include <stdio.h>
#include <sys/time.h>
#include <sys/timerfd.h>

using json = nlohmann::json;

MQTTHandler::MQTTHandler(EventHandler& eventHandler, ConfigHandler& configHandler, std::string const& host, uint16_t const& port,
                         std::string const& username, std::string const& password)
    : configHandler(configHandler) {
    mosquitto_lib_init();
    mqtt = mosquitto_new(NULL, true, this);
    mosquitto_username_pw_set(mqtt, username.c_str(), password.c_str());
    int r = mosquitto_connect(mqtt, host.c_str(), port, 60);
    if (r) {
        std::cout << "warning: could not connect to mqtt broker: " << r << std::endl;
    } else {
        mqtt_socket = mosquitto_socket(mqtt);
        if (mqtt_socket != -1) {
            mosquitto_subscribe(mqtt, nullptr, mqtt_topic.c_str(), 0);
            timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
            if (timer_fd == -1) {
                std::cout << "warning: could not create mqtt timer" << std::endl;
            } else {
                itimerspec ts = {};
                ts.it_interval.tv_sec = 0;
                ts.it_interval.tv_nsec = 100 * 1000 * 1000; // 100ms
                ts.it_value = ts.it_interval;
                timerfd_settime(timer_fd, 0, &ts, nullptr);

                handler = std::bind(&MQTTHandler::process_socket, this, std::placeholders::_1);
                eventHandler.register_socket(timer_fd, timerHandler);
                timerHandler = std::bind(&MQTTHandler::process_timer, this, std::placeholders::_1);
                eventHandler.register_socket(mqtt_socket, handler, EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP);

                mosquitto_message_callback_set(mqtt, message_callback);
            }

        } else {
            std::cout << "warning: failed to get mqtt_socket" << std::endl;
        }
    }
}

MQTTHandler::~MQTTHandler() {
    mosquitto_disconnect(mqtt);
    mosquitto_destroy(mqtt);
    mosquitto_lib_cleanup();
}

void MQTTHandler::upload_menuentries(MAC const& mac, std::unordered_map<std::string, std::string> const& menuentries) {
    // clang-format off
    json payload = {
        {"dev", {
            {"ids", "remote-bootselect"},
            {"name", "Remote Bootselect"}}
        },
        {"o", {
              {"name", "remote-bootselect"},
              {"url", "https://github.com/bobby3605/remote-bootselect/"}}
        },
        {"cmps", {}}
    };
    char source[18];
    snprintf(source, sizeof(source), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // clang-format on
    for (const auto& [id, title] : menuentries) {
        payload["cmps"][id] = {
            {"p",             "button"                      },
            {"unique_id",     id                            },
            {"name",          title                         },
            {"command_topic", mqtt_topic                    },
            {"payload_press", std::string(source) + " " + id}
        };
    }

    std::string payload_str = payload.dump();
    //    mosquitto_publish(mqtt, NULL, discovery_topic.c_str(), 0, "", 0, true);

    mosquitto_publish(mqtt, NULL, discovery_topic.c_str(), payload_str.size(), payload_str.c_str(), 0, true);
}

void MQTTHandler::process_socket(uint32_t events) {
    if (events & EPOLLIN) {
        mosquitto_loop_read(mqtt, 1);
    }
    if (events & EPOLLOUT) {
        mosquitto_loop_write(mqtt, 1);
    }
}

void MQTTHandler::process_timer(uint32_t events) {
    uint64_t exp;
    read(timer_fd, &exp, sizeof(exp));
    mosquitto_loop_misc(mqtt);
}

void message_callback(mosquitto* mqtt, void* obj, const mosquitto_message* msg) {
    std::stringstream config_message((char*)msg->payload);
    reinterpret_cast<MQTTHandler*>(obj)->configHandler.process_config(config_message);
}
