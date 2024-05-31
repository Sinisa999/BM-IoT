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

const unsigned short multicast_port = 1900;
const char* multicast_address = "239.255.255.250";
const char* sensor_location = "127.0.0.1 "; // Zameniti sa stvarnom adresom

// Structure to hold device information
struct DeviceInfo {

    //TODO timeout alive
    std::string id;
    std::string name;
    std::string status;
    bool connected;
};



int sockfd; 
/*
void process_json_response(const std::string& response, std::unordered_map<std::string, DeviceInfo>& connected_devices) {
    // Parsiranje primljenih podataka o prikljucenom uredjaju
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errors;

    std::istringstream iss(response);
    if (!Json::parseFromStream(builder, iss, &root, &errors)) {
        std::cerr << "Failed to parse JSON: " << errors << std::endl;
        return;
    }
    
    // Proveravanje koji tip senzora/aktuatora je u pitanju
    // TODO Dodati sve uredjaje
    bool has_temp = root.isMember("temperature");
    bool has_hum = root.isMember("humidity");
    bool has_light = root.isMember("light");
    bool has_pH = root.isMember("pH");

    std::string name = root["name"].asString();
    std::string status = root["status"].asString();
    
    
    if(has_temp){
    	double temperature = root["temperature"].asDouble();
	    std::string id = root["id"].asString();
        if (connected_devices.find(id) == connected_devices.end()) {
            // Dodavanje novog uredjaja ako nije vec u mapi
            DeviceInfo device_info;
            device_info.id = id;
            device_info.name = name;
            device_info.status = status;
            device_info.connected = true; 
            connected_devices[id] = device_info;
        }
    	std::cout << "Client ID: " << root["id"].asInt() << std::endl; 
    	std::cout << "Client Name: " << name << std::endl;
        std::cout << "Status: " << status << std::endl;
        std::cout << "Temperature: " << temperature << "°C" << std::endl;	
    }
    
    if(has_hum){
    	double humidity = root["humidity"].asDouble();
	    std::string id = root["id"].asString();
        if (connected_devices.find(id) == connected_devices.end()) {
            
            DeviceInfo device_info;
            device_info.id = id;
            device_info.name = name;
            device_info.status = status;
            device_info.connected = true; 
            connected_devices[id] = device_info;
        }
    	std::cout << "Client ID: " << root["id"].asInt() << std::endl; 
    	std::cout << "Client Name: " << name << std::endl;
        std::cout << "Status: " << status << std::endl;
        std::cout << "Humidity: " << humidity << "%" << std::endl;
    }
    
    if(has_light){
    	double light = root["light"].asDouble();
	    std::string id = root["id"].asString();
        if (connected_devices.find(id) == connected_devices.end()) {
            
            DeviceInfo device_info;
            device_info.id = id;
            device_info.name = name;
            device_info.status = status;
            device_info.connected = true; 
            connected_devices[id] = device_info;
        }
    	std::cout << "Client ID: " << root["id"].asInt() << std::endl; 
    	std::cout << "Client Name: " << name << std::endl;
	    std::cout << "Status: " << status << std::endl;
	    std::cout << "Light: " << light << "%" << std::endl;
    }
    
    if(has_pH){
    	double pH = root["pH"].asDouble();
	    std::string id = root["id"].asString();
        if (connected_devices.find(id) == connected_devices.end()) {
            
            DeviceInfo device_info;
            device_info.id = id;
            device_info.name = name;
            device_info.status = status;
            device_info.connected = true; 
            connected_devices[id] = device_info;
        }
    	std::cout << "Client ID: " << root["id"].asInt() << std::endl; 
    	std::cout << "Client Name: " << name << std::endl;
	    std::cout << "Status: " << status << std::endl;
	    std::cout << "pH: " << pH << "" << std::endl;
    }
    
}
*/
void process_discovery_message(const std::string& message) { // M-SEARCH
    std::cout << "Received message from device:" << std::endl;
    std::cout << message << std::endl;
}

void send_confirmation(int sockfd, struct sockaddr_in client_addr) { // RESPONSE
    std::string confirmation = "HTTP/1.1 200 OK\r\n"
                               "ST: ssdp:all\r\n"
                               "\r\n";
    sendto(sockfd, confirmation.c_str(), confirmation.length(), 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
}

void print_connected(std::unordered_map<std::string, DeviceInfo> connected_devices){
	// Ispis trenutno povezanih uredjaja
    std::cout << "\nConnected devices:" << std::endl;
    for (const auto& entry : connected_devices) {
        std::cout << "ID: " << entry.second.id << ", Name: " << entry.second.name << ", Status: "<< entry.second.status << std::endl;
    }

    std::cout<< "********************************************************************************\n" <<std::endl;


}


int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[4096];
    
    // Mapa za cuvanje informacija o povezanim uredjajima
    std::unordered_map<std::string, DeviceInfo> connected_devices;

    // Pravljenje UDP socket-a
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
        socklen_t server_len = sizeof(server_addr);
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0,(struct sockaddr*)&server_addr, &server_len);
        if (bytes_received < 0) {
            std::cerr << "Receive failed" << std::endl;
            continue;
        }

        buffer[bytes_received] = '\0'; 
        std::string message(buffer);
        

        // Provera tipa primljene poruke

        if (message.find("M-SEARCH") != std::string::npos) {

            process_discovery_message(message);
            
            // Poslati nazad RESPONSE poruku uredjaju da je povezan
            send_confirmation(sockfd, server_addr);
            
        }else if(message.find("NTS: ssdp:alive") != std::string::npos){ // alive

	   //TODO timeout

            process_discovery_message(message);
            
            // Poslati nazad poruku uredjaju da je povezan
            send_confirmation(sockfd, server_addr);
            
            // Ispis trenutno povezanih uredjaja
            print_connected(connected_devices);
            
        }else if (message.find("NTS: ssdp:byebye") != std::string::npos) { // byebye

            process_discovery_message(message);

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

                    std::cout << "Found UUID: " << id << "."<< std::endl; // Za debagovanje.

                    // Postavljanje statusa pronadjenog uredjaja na Offline i njegovo uklanjanje iz mape povezanih uredjaja
                    auto it = connected_devices.find(id);
                    if (it != connected_devices.end()) {
                        
                        it->second.status = "Offline";
                        std::cout << "Device ID: " << it->first << " went Offline" << std::endl;
                        std::cout<< "********************************************************************************\n" <<std::endl;
                        // Uklanjanje uredjaja
                        connected_devices.erase(it);
                    } else {

                        // Ako uredjaj sa parsiranim ID-jem nije pronadjen. Za debagovanje.
                        std::cout << "Device with ID " << id << " not found in connected devices map." << std::endl;
                    }

                    break;
                }
            }
	    }
	else {
            
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
         
    }

    close(sockfd);
    return 0;
}


