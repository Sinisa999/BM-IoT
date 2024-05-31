#include <iostream>
#include <fstream>
#include <string>
#include <mosquitto.h>
#include <jsoncpp/json/json.h> // JSON library
#include "httplib.h" // HTTP library (make sure you have httplib.h)
#include <thread> // Include thread library for sleep
#include <chrono> // Include chrono library for time duration

const char *mqtt_host = "localhost";
const int mqtt_port = 1883;
const char *topic_humidity = "sensors/humidity";

int main() {
    mosquitto_lib_init();
    struct mosquitto *mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq) {
        std::cerr << "Error: Unable to initialize MQTT client.\n";
        return 1;
    }
    if (mosquitto_connect(mosq, mqtt_host, mqtt_port, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Error: Unable to connect to MQTT broker.\n";
        return 1;
    }

    httplib::Client cli("http://localhost:8080");

    while (true) {
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

        double humidity = root["humidity"].asDouble();
        std::string payload = std::to_string(humidity);
        mosquitto_publish(mosq, NULL, topic_humidity, payload.length(), payload.c_str(), 0, false);

        std::cout << "Published humidity: " << humidity << "%\n";
        std::this_thread::sleep_for(std::chrono::seconds(5)); // Adjust interval as needed
    }

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    return 0;
}

