#include <iostream>
#include <cstring>
#include <mosquitto.h>
#include "httplib.h" // Include the httplib library

// Define MQTT broker address and ports
#define MQTT_SERVER_ADDRESS "localhost"
#define MQTT_SERVER_PORT 1883

// Define MQTT topic for the relay actuator
#define RELAY_TOPIC "actuators/relay_vent"

// Define command values for the relay actuator
#define RELAY_ON "ON"
#define RELAY_OFF "OFF"

// HTTP server address
#define HTTP_SERVER_ADDRESS "localhost"
#define HTTP_SERVER_PORT 8080
#define HTTP_SERVER_ENDPOINT "/update_relay_state"

// Function to send HTTP request to update relay state in environment
void notify_environment_of_relay_state(const std::string& state) {
    httplib::Client cli(HTTP_SERVER_ADDRESS, HTTP_SERVER_PORT);
    httplib::Headers headers = {
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    httplib::Params params = {
        {"vent", state}
    };
    auto res = cli.Post(HTTP_SERVER_ENDPOINT, headers, params);
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

// MQTT callback function
void on_message_callback(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (message->payloadlen) {
        std::cout << "Message received on topic: " << message->topic << std::endl;
        std::cout << "Payload: " << (char*)message->payload << std::endl;

        // Process the received message here
        if (strcmp((char*)message->payload, RELAY_ON) == 0) {
            // Turn on the relay actuator
            std::cout << "Relay actuator turned ON" << std::endl;
            notify_environment_of_relay_state(RELAY_ON);
        } else if (strcmp((char*)message->payload, RELAY_OFF) == 0) {
            // Turn off the relay actuator
            std::cout << "Relay actuator turned OFF" << std::endl;
            notify_environment_of_relay_state(RELAY_OFF);
        }
    }
}

int main(int argc, char* argv[]) {
    struct mosquitto* mosq = NULL;

    // Initialize mosquitto library
    mosquitto_lib_init();

    // Create mosquitto client instance
    mosq = mosquitto_new("relay_vent_actuator", true, NULL);
    if (!mosq) {
        std::cerr << "Error: Unable to create mosquitto client instance" << std::endl;
        mosquitto_lib_cleanup();
        return 1;
    }

    // Connect to the MQTT broker
    if (mosquitto_connect(mosq, MQTT_SERVER_ADDRESS, MQTT_SERVER_PORT, 60) != MOSQ_ERR_SUCCESS) {
        std::cerr << "Error: Unable to connect to MQTT broker" << std::endl;
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }
    std::cout << "Connected to MQTT broker" << std::endl;

    // Subscribe to the relay topic
    mosquitto_subscribe(mosq, NULL, RELAY_TOPIC, 1);
    std::cout << "Subscribed to relay topic" << std::endl;

    // Set message callback
    mosquitto_message_callback_set(mosq, on_message_callback);

    // Enter mosquitto loop
    mosquitto_loop_forever(mosq, -1, 1);

    // Cleanup and disconnect from the MQTT broker
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    std::cout << "Disconnected from MQTT broker" << std::endl;

    return 0;
}

