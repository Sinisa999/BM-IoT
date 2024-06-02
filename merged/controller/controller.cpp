#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jsoncpp/json/json.h> // JSON biblioteka
#include <string>
#include <sstream>
#include <unordered_map> // Za cuvanje podataka o prikljucenim uredjajima
#include <algorithm>
#include <mosquitto.h>

// Define MQTT broker address and ports
#define MQTT_SERVER_ADDRESS "localhost"
#define MQTT_SERVER_PORT 1883

// Define MQTT topics for sensors and actuators
#define SENSORS "sensors/+"
#define TEMPERATURE_TOPIC "sensors/temperature"
#define RELAY_VENT_TOPIC "actuators/relay_vent"
// za pumpu
#define PUMP_TOPIC "actuators/relay_pump"
#define HUMIDITY_TOPIC "sensors/humidity"

// Define command values for actuators
#define COMMAND_ON "ON"
#define COMMAND_OFF "OFF"

const unsigned short multicast_port = 1900;
const char* multicast_address = "239.255.255.250";
  const char* sensor_location = "127.0.0.1 "; // Zameniti sa stvarnom adresom

// Structure to hold device information
struct DeviceInfo {
    std::string id;
    std::string name;
    std::string status;
    bool connected;
};

int sockfd;
std::unordered_map<std::string, DeviceInfo> connected_devices;


// MQTT callback function
void on_message_callback(struct mosquitto* mosq, void* userdata, const struct mosquitto_message* message) {
    if (message->payloadlen) {
	std::cout<< "********************************************************************************\n" <<std::endl;
        std::cout << "Message received on topic: " << message->topic << std::endl;
        std::cout << "Payload: " << (char*)message->payload << std::endl;
	std::cout<< "********************************************************************************\n" <<std::endl;

        // Process the received message here
        if (strcmp(message->topic, TEMPERATURE_TOPIC) == 0) {
            double temperature = std::stod((char*)message->payload);
            const char* command = (temperature > 29.0) ? COMMAND_ON : (temperature < 24.0) ? COMMAND_OFF : nullptr;

            if (command) {
                // Publish command to turn on/off ventilator
                mosquitto_publish(mosq, NULL, RELAY_VENT_TOPIC, strlen(command), command, 1, true);
                // Output to terminal
		std::cout<< "********************************************************************************\n" <<std::endl;
                std::cout << "Published command: " << command << " on topic: " << RELAY_VENT_TOPIC << std::endl;
		std::cout<< "********************************************************************************\n" <<std::endl;
            }
        }

        if (strcmp(message->topic, HUMIDITY_TOPIC) == 0) {
            double humidity = std::stod((char*)message->payload);
            const char* command = (humidity < 50) ? COMMAND_ON : (humidity > 70.0) ? COMMAND_OFF : nullptr;

            if (command) {
                // Publish command to turn on/off pump
                mosquitto_publish(mosq, NULL, PUMP_TOPIC, strlen(command), command, 1, true);
                // Output to terminal
		std::cout<< "********************************************************************************\n" <<std::endl;
                std::cout << "Published command: " << command << " on topic: " << PUMP_TOPIC << std::endl;
		std::cout<< "********************************************************************************\n" <<std::endl;
            }
        }
    }
}

void process_discovery_message(const std::string& message, const struct sockaddr_in& sender_addr) { // M-SEARCH
    char sender_ip[INET_ADDRSTRLEN]; // Buffer to hold the sender's IP address
    inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, INET_ADDRSTRLEN); // Convert binary IP to string


    std::cout << "Received message from device:" << std::endl;
    std::cout << message << std::endl;
}

void send_confirmation(int sockfd, struct sockaddr_in client_addr) { // RESPONSE
    std::string confirmation = "HTTP/1.1 200 OK\r\n"
                               "ST: ssdp:all\r\n"
                               "\r\n";
    sendto(sockfd, confirmation.c_str(), confirmation.length(), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
}

void print_connected(const std::unordered_map<std::string, DeviceInfo>& connected_devices){
    // Ispis trenutno povezanih uredjaja
    std::cout<< "********************************************************************************\n" <<std::endl;
    std::cout << "\nConnected devices:" << std::endl;
    for (const auto& entry : connected_devices) {
        std::cout << "ID: " << entry.second.id << ", Name: " << entry.second.name << ", Status: "<< entry.second.status << std::endl;
    }
    std::cout<< "********************************************************************************\n" <<std::endl;
}

int main(int argc, char* argv[]) {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[4096];

    // Initialize mosquitto library
    mosquitto_lib_init();

    // Create mosquitto client instance
    struct mosquitto* mosq = mosquitto_new("controller_client", true, NULL);
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

    // Subscribe to sensor topics
    mosquitto_subscribe(mosq, NULL, SENSORS, 1);
    std::cout << "Subscribed to " << SENSORS << " topic" << std::endl;

    // Set message callback
    mosquitto_message_callback_set(mosq, on_message_callback);

    // Create UDP socket
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    // Server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(multicast_port);

    // Bind socket to server address
    if (bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }

    // Set up client address for receiving
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr(multicast_address);
    client_addr.sin_port = htons(multicast_port);

    std::string response = "";

    while (true) {
        socklen_t sender_len = sizeof(client_addr);
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &sender_len);
        if (bytes_received < 0) {
            std::cerr << "Receive failed" << std::endl;
            continue;
        }

        buffer[bytes_received] = '\0';
        std::string message(buffer);

        // Provera tipa primljene poruke
        if (message.find("M-SEARCH") != std::string::npos) {
            process_discovery_message(message, client_addr);
            // Poslati nazad RESPONSE poruku uredjaju da je povezan
            send_confirmation(sockfd, client_addr);
        } else if (message.find("NTS: ssdp:alive") != std::string::npos) { // alive
            process_discovery_message(message, client_addr);
            // Poslati nazad poruku uredjaju da je povezan
            send_confirmation(sockfd, client_addr);
            // Ispis trenutno povezanih uredjaja
            print_connected(connected_devices);
        } else if (message.find("NTS: ssdp:byebye") != std::string::npos) { // byebye
            process_discovery_message(message, client_addr);
            // Procesuiraj NOTIFY poruku tipa byebye
            std::istringstream iss(message);
            std::string line;
            while (std::getline(iss, line)) {
                // Pretraga linije koja sadrzi ID uredjaja
                size_t pos = line.find("USN: uuid:");
                if (pos != std::string::npos) {
                    // Izdvajanje ID-ja
                    std::string id = line.substr(pos + 10); // Pomeraj za duzinu "USN: uuid:"
                    id.erase(std::remove_if(id.begin(), id.end(), ::isspace), id.end()); // Uklanjanje potencijalnog whitespace-a
                    std::cout << "Found UUID: " << id << "." << std::endl; // Za debagovanje.
                    // Postavljanje statusa pronadjenog uredjaja na Offline i njegovo uklanjanje iz mape povezanih uredjaja
                    auto it = connected_devices.find(id);
                    if (it != connected_devices.end()) {
                        it->second.status = "Offline";
                        std::cout << "Device ID: " << it->first << " went Offline" << std::endl;
                        std::cout << "********************************************************************************\n" << std::endl;
                        // Uklanjanje uredjaja
                        connected_devices.erase(it);
                    } else {
                        // Ako uredjaj sa parsiranim ID-jem nije pronadjen. Za debagovanje.
                        std::cout << "Device with ID " << id << " not found in connected devices map." << std::endl;
                    }
                    break;
                }
            }
        } else {
            // Dodavanje novog uredjaja u mapu prikljucenih uredjaja
            // Parsiranje JSON objekta poslatog od strane uredjaja
            Json::CharReaderBuilder builder;
            Json::Value root;
            std::string errors;
            std::istringstream iss(message);
            if (!Json::parseFromStream(builder, iss, &root, &errors)) {
                std::cerr << "Failed to parse JSON: " << errors << std::endl;
                continue;
            }

            std::string id = root["id"].asString();
            if (connected_devices.find(id) == connected_devices.end()) {
                DeviceInfo device_info;
                device_info.id = id;
                device_info.name = root["name"].asString();
                device_info.status = root["status"].asString();
                connected_devices[id] = device_info;

                // Ispisivanje podataka o novom prikljucenom uredjaju
                std::cout << "\nNew device connected:" << std::endl;
                std::cout << "ID: " << device_info.id << ", Name: " << device_info.name << ", Status: " << device_info.status << std::endl;
            }
        }
        mosquitto_loop(mosq, -1, 1);
    }

    close(sockfd);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    std::cout << "Disconnected from MQTT broker" << std::endl;
    return 0;
}

