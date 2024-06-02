#include <iostream>
#include <csignal> // For SIGINT
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jsoncpp/json/json.h> // JSON library
#include <string>
#include <fstream>
#include <chrono>
#include <thread>
#include <mosquitto.h>
#include "httplib.h" // HTTP library
#include <sstream> // For string streams

const unsigned short multicast_port = 1900;
const char* multicast_address = "239.255.255.250";
const char *mqtt_host = "localhost";
const int mqtt_port = 1883;
const char *topic_temperature = "sensors/temperature";

// Variable to check if CTRL+C was pressed
volatile sig_atomic_t ctrl_c_received = 0;

// Function for CTRL+C signal
void ctrl_c_handler(int signal) {
    if (signal == SIGINT) {
        ctrl_c_received = 1;
    }
}

// Function to generate a unique device ID
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
                            "Pozdrav od Senzora Temperature Vazduha\r\n"
                            "\r\n";

    sendto(sockfd, discovery.c_str(), discovery.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void send_json_response(int sockfd, struct sockaddr_in server_addr, std::string id) {
    Json::Value client_state;
    client_state["id"] = id;
    client_state["name"] = "Senzor Temperature Vazduha";
    client_state["status"] = "Online";

    Json::StreamWriterBuilder builder;
    std::string response = Json::writeString(builder, client_state);

    sendto(sockfd, response.c_str(), response.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

int receive_confirmation(int sockfd, struct sockaddr_in server_addr, std::string &controller_ip) {
    char buffer[2048];
    socklen_t server_len = sizeof(server_addr);

    int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &server_len);
    if (bytes_received < 0) {
        std::cerr << "Receive confirmation failed" << std::endl;
        return 0;
    }

    buffer[bytes_received] = '\0';
    std::string message(buffer);

    std::cout << "Received confirmation message from controller:" << std::endl;
    std::cout << message << std::endl;

    // Extract the IP address of the controller
    controller_ip = inet_ntoa(server_addr.sin_addr);
    std::cout << "Controller IP: " << controller_ip << std::endl;

    return 1;
}

int receive_confirmation_with_timeout(int sockfd, struct sockaddr_in server_addr, int timeout_sec, std::string &controller_ip) {
    char buffer[2048];
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
        std::cout << "Received confirmation message from controller:" << std::endl;
        std::cout << message << std::endl;

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
}

void send_byebye(int sockfd, struct sockaddr_in server_addr, std::string id) {
    std::string byebye = "NOTIFY * HTTP/1.1\r\n"
                         "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                         "NTS: ssdp:byebye\r\n"
                         "USN: uuid:" + id + "\r\n"
                         "\r\n";

    sendto(sockfd, byebye.c_str(), byebye.length(), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
}

void publish_temperature(struct mosquitto *mosq, httplib::Client &cli) {
    while (!ctrl_c_received) {
        auto res = cli.Get("/environment");
        if (!res || res->status != 200) {
            std::cerr << "Error: Unable to fetch environment data.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        Json::Value root;
        Json::CharReaderBuilder reader;
        std::string errs;
        std::istringstream s(res->body);
        if (!Json::parseFromStream(reader, s, &root, &errs)) {
            std::cerr << "Error: Unable to parse JSON response.\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        double temperature = root["temperature"].asDouble();
        std::string payload = std::to_string(temperature);
        mosquitto_publish(mosq, NULL, topic_temperature, payload.length(), payload.c_str(), 0, false);

        std::cout << "Published temperature: " << temperature << "°C\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));
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
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        std::cerr << "Error: Unable to initialize MQTT client.\n";
        return 1;
    }
    if (mosquitto_connect(mosq, controller_ip.c_str(), mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Error: Unable to connect to MQTT broker.\n";
        return 1;
    }

    httplib::Client cli("http://localhost:8080");

    std::thread mqtt_thread(publish_temperature, mosq, std::ref(cli));

    while (!ctrl_c_received) {
        send_notify(sockfd, server_addr, id);
        std::cout << "NOTIFY sent.\n" << std::endl;

        receive_confirmation(sockfd, server_addr, controller_ip);

        send_json_response(sockfd, server_addr, id);
        std::cout << "Device status sent.\n" << std::endl;
        std::cout << "************************************" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    send_byebye(sockfd, server_addr, id);

    mqtt_thread.join();

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    close(sockfd);
    return 0;
}

