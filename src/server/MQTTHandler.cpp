#include "MQTTHandler.hpp"
#include "mosquitto.h"
#include "src/server/ConfigHandler.hpp"
#include <iostream>
#include <json.hpp>
#include <stdio.h>
#include <sys/time.h>
#include <sys/timerfd.h>

using json = nlohmann::json;

void message_callback(mosquitto* mqtt, void* obj, const mosquitto_message* msg) {
    if (msg->payloadlen > 0) {
        std::stringstream config_message((char*)msg->payload);
        reinterpret_cast<MQTTHandler*>(obj)->configHandler.process_config(config_message);
    }
}

int state_callback(mosquitto* mqtt, void* obj, const mosquitto_message* msg) {
    message_callback(mqtt, obj, msg);
    // FIXME:
    // This doesn't really work, it just gets the first message then returns (if there even is a message)
    return 1;
}

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

void MQTTHandler::get_state(std::string const& host, int const& port, std::string const& username, std::string const& password) {
    // FIXME:
    // Don't use this
    // MQTT doesn't have a way to say "get all retained messages under this wildcard"
    // or a way to query which topics exist under a wildcard
    // or even an easy way to put a timeout on these helper functions
    // I can make my own with a timeout, but it's not that important
    std::string topic = mqtt_topic + "/state/+";
    mosquitto_subscribe_callback(state_callback, this, topic.c_str(), 0, host.c_str(), port, NULL, 0, true, username.c_str(),
                                 password.c_str(), NULL, NULL);
}

void MQTTHandler::upload_menuentries(MAC const& mac, std::unordered_map<std::string, std::string> const& menuentries) {

    json options = {};
    json id_to_title = {};
    json title_to_id = {};
    for (const auto& [id, title] : menuentries) {
        options.push_back(title);
        id_to_title[id] = title;
        title_to_id[title] = id;
    }

    // clang-format off
    json payload = {
        {"dev", {
            {"ids", "remote_bootselect"},
            {"name", "Remote Bootselect"}}
        },
        {"o", {
              {"name", "remote_bootselect"},
              {"url", "https://github.com/bobby3605/remote-bootselect/"}}
        },
        {"cmps", {}}
    };
    char source_tmp[18];
    snprintf(source_tmp, sizeof(source_tmp), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::string source(source_tmp);

    std::string command_template = "{% set map = " + title_to_id.dump(0) + " %}" + source + " {{ map[value] }}";
    std::string value_template = "{% set map = " + id_to_title.dump(0) + " %}{{ map[value] }}";

    payload["cmps"][source] = {
    {"p", "select"},
    {"name", source},
    {"options", options},
    {"unique_id", source},
    {"command_topic", mqtt_topic},
    {"command_template", command_template},
    {"state_topic", mqtt_topic + "/state/" + source},
    {"value_template", value_template}
    };

    std::string payload_str = payload.dump();
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

void MQTTHandler::publish_state(MAC const& mac, std::string const& entry){

    char source_tmp[18];
    snprintf(source_tmp, sizeof(source_tmp), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    std::string source(source_tmp);

    std::string topic = mqtt_topic + "/state/" + source;

    // TODO:
    // subscribe to this once on startup
    mosquitto_publish(mqtt, NULL, topic.c_str(), entry.size(), entry.c_str(), 0, true);
}

