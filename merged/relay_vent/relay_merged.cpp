#include <iostream>
#include <csignal>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jsoncpp/json/json.h>
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <mosquitto.h>
#include "httplib.h"

const unsigned short multicast_port = 1900;
const char* multicast_address = "239.255.255.250";
const char* mqtt_host = "localhost";
const int mqtt_port = 1883;
const char* relay_topic = "actuators/relay_vent";
const char* relay_on = "ON";
const char* relay_off = "OFF";
const char* http_server_address = "localhost";
const int http_server_port = 8080;
const char* http_server_endpoint = "/update_relay_state";

volatile sig_atomic_t ctrl_c_received = 0;

void ctrl_c_handler(int signal) {
    if (signal == SIGINT) {
        ctrl_c_received = 1;
    }
}

std::string generate_unique_id() {
    std::srand(static_cast<unsigned int>(std::time(nullptr)));
    int unique_id = std::rand() % 9000 + 1000;
    return std::to_string(unique_id);
}

void send_discovery(int sockfd, struct sockaddr_in server_addr) {
    std::string discovery = "M-SEARCH * HTTP/1.1\r\n"
                            "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                            "MAN: \"ssdp:discover\"\r\n"
                            "MX: 1\r\n"
                            "ST: ssdp:all\r\n"
                            "Pozdrav od Releja za ventilator\r\n"
                            "\r\n";
    sendto(sockfd, discovery.c_str(), discovery.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "Sent discovery message: " << discovery << std::endl;
}

void send_json_response(int sockfd, struct sockaddr_in server_addr, std::string id) {
    Json::Value client_state;
    client_state["id"] = id;
    client_state["name"] = "Relej za ventilator";
    client_state["status"] = "Online";

    Json::StreamWriterBuilder builder;
    std::string response = Json::writeString(builder, client_state);

    sendto(sockfd, response.c_str(), response.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    //std::cout << "Sent JSON response: " << response << std::endl;
}

int receive_confirmation_with_timeout(int sockfd, struct sockaddr_in server_addr, int timeout_sec, std::string &controller_ip) {
    char buffer[1024];
    socklen_t server_len = sizeof(server_addr);
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        std::cerr << "Error setting socket timeout" << std::endl;
        return 0;
    }

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    int ready = select(sockfd + 1, &fds, nullptr, nullptr, &tv);
    if (ready < 0) {
        std::cerr << "Error in select" << std::endl;
        return 0;
    } else if (ready == 0) {
        std::cout << "Timeout expired, no data received" << std::endl;
        return 0;
    } else {
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &server_len);
        if (bytes_received < 0) {
            std::cerr << "Receive confirmation failed" << std::endl;
            return 0;
        }
        buffer[bytes_received] = '\0';
        std::string message(buffer);
        std::cout << "Received confirmation message from controller: " << message << std::endl;

        // Extract the IP address of the controller
        controller_ip = inet_ntoa(server_addr.sin_addr);
        std::cout << "Controller IP: " << controller_ip << std::endl;
        return 1;
    }
}

void send_notify(int sockfd, struct sockaddr_in server_addr, std::string id) {
    std::string notify = "NOTIFY * HTTP/1.1\r\n"
                         "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                         "NTS: ssdp:alive\r\n"
                         "USN: uuid:" + id + "\r\n"
                         "CACHE-CONTROL: max-age=1800\r\n"
                         "LOCATION: http://localhost/device\r\n"
                         "\r\n";
    sendto(sockfd, notify.c_str(), notify.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "Sent notify message: " << notify << std::endl;
}

void send_byebye(int sockfd, struct sockaddr_in server_addr, std::string id) {
    std::string byebye = "NOTIFY * HTTP/1.1\r\n"
                         "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                         "NTS: ssdp:byebye\r\n"
                         "USN: uuid:" + id + "\r\n"
                         "\r\n";
    sendto(sockfd, byebye.c_str(), byebye.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    std::cout << "Sent byebye message: " << byebye << std::endl;
}

void notify_environment_of_relay_state(const std::string& state) {
    httplib::Client cli(http_server_address, http_server_port);
    httplib::Headers headers = {{"Content-Type", "application/x-www-form-urlencoded"}};
    httplib::Params params = {{"vent", state}};
    auto res = cli.Post(http_server_endpoint, headers, params);
    if (res) {
        if (res->status == 200) {
            std::cout << "Relay state notified to environment successfully" << std::endl;
        } else {
            std::cerr << "Failed to notify relay state to environment: " << res->status << " - " << res->body << std::endl;
        }
    } else {
        auto err = res.error();
        std::cerr << "Failed to notify relay state to environment: " << httplib::to_string(err) << std::endl;
    }
}

void on_message_callback(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (message->payloadlen) {
        std::cout << "Message received on topic: " << message->topic << std::endl;
        std::cout << "Payload: " << (char*)message->payload << std::endl;

        if (strcmp((char*)message->payload, relay_on) == 0) {
            std::cout << "Relay actuator turned ON" << std::endl;
            notify_environment_of_relay_state(relay_on);
        } else if (strcmp((char*)message->payload, relay_off) == 0) {
            std::cout << "Relay actuator turned OFF" << std::endl;
            notify_environment_of_relay_state(relay_off);
        }
    }
}

int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    std::string controller_ip;
    std::string id = generate_unique_id();

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(multicast_address);
    server_addr.sin_port = htons(multicast_port);

    ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_address);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        std::cerr << "Joining multicast group failed" << std::endl;
        return 1;
    }

    bool controller_found = false;

    while (!controller_found) {
        send_discovery(sockfd, server_addr);
        std::cout << "M-SEARCH sent." << std::endl;

        bool flag_received = receive_confirmation_with_timeout(sockfd, server_addr, 5, controller_ip);

        if (flag_received) {
            controller_found = true;
        }
    }

    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = ctrl_c_handler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, nullptr);

    mosquitto_lib_init();
    struct mosquitto* mosq = mosquitto_new("relay_vent_actuator", true, NULL);
    if (!mosq) {
        std::cerr << "Error: Unable to initialize MQTT client.\n";
        return 1;
    }

    mosquitto_message_callback_set(mosq, on_message_callback);

    if (mosquitto_connect(mosq, controller_ip.c_str(), mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Error: Unable to connect to MQTT broker.\n";
        return 1;
    }

    if (mosquitto_subscribe(mosq, NULL, relay_topic, 0) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Error: Unable to subscribe to MQTT topic\n";
        return 1;
    }

    while (!ctrl_c_received) {
        send_notify(sockfd, server_addr, id);
        std::cout << "NOTIFY sent.\n" << std::endl;

        receive_confirmation_with_timeout(sockfd, server_addr, 3, controller_ip);

        send_json_response(sockfd, server_addr, id);
        std::cout << "Device status sent.\n" << std::endl;
        std::cout << "************************************" << std::endl;

        mosquitto_loop(mosq, -1, 1);
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    send_byebye(sockfd, server_addr, id);

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    close(sockfd);

    return 0;
}

