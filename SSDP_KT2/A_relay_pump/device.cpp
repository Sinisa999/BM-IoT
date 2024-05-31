#include <iostream>
#include <csignal> // Za SIGINT
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <jsoncpp/json/json.h> // JSON biblioteka
#include <string>
#include <fstream>
#include <chrono> 
#include <thread>

const unsigned short multicast_port = 1900;
const char* multicast_address = "239.255.255.250";

// Promenljiva za proveru da li se desilo CTRL+C 
volatile sig_atomic_t ctrl_c_received = 0;

// Funkcija za CTRL+C signal
void ctrl_c_handler(int signal) {
    if (signal == SIGINT) {
        ctrl_c_received = 1;
    }
}

// Funkcija za generisanje ID-ja uredaja
std::string generate_unique_id() {
    // Cetvorocifreni broj
    // TODO Provera da li postoji na mrezi uredjaj sa istim ID-jem
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
                            "Pozdrav od Releja za pumpu\r\n"
                            "\r\n";
                            
    sendto(sockfd, discovery.c_str(), discovery.length(), 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
}

void send_json_response(int sockfd, struct sockaddr_in server_addr, std::string id) {

    // Kreiranje JSON objekta za predstavu stanja uredjaja
    Json::Value client_state;
    client_state["id"] = id;
    client_state["name"] = "Relej za pumpu";
    client_state["status"] = "Online";
    

    // Konvertovanje JSON objekta u string 
    Json::StreamWriterBuilder builder;
    std::string response = Json::writeString(builder, client_state);

    // Slanje podataka o uredjaju
    sendto(sockfd, response.c_str(), response.length(), 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
}

int receive_confirmation(int sockfd, struct sockaddr_in server_addr) {
    char buffer[1024];
    socklen_t server_len = sizeof(server_addr);
    
    int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0,(struct sockaddr*)&server_addr, &server_len);
    if (bytes_received < 0) {
        std::cerr << "Receive confirmation failed" << std::endl;
        return 0;
    }

    buffer[bytes_received] = '\0'; 
    std::string message(buffer);

    // Ispisivanje potvrde
    std::cout << "Received confirmation message from controller:" << std::endl;
    std::cout << message << std::endl;

    return 1;
}

// Funkcija za prijem potvrde od kontrolera sa timeout-om
int receive_confirmation_with_timeout(int sockfd, struct sockaddr_in server_addr, int timeout_sec) {
    char buffer[1024];
    socklen_t server_len = sizeof(server_addr);
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;

    // Postavljanje vremenskog ogranicenja za socket
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        std::cerr << "Error setting socket timeout" << std::endl;
        return 0;
    }

    // Select funkcija za pracenje dogadjaja na soketu sa vremenskim ogranicenjem (jer je recvfrom blokirajuca funkcija)
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(sockfd, &fds);

    // Cekanje na dogadjaj na soketu (prijem poruke ili istek timeouta)
    int ready = select(sockfd + 1, &fds, nullptr, nullptr, &tv);
    if (ready < 0) {
        // Greska u select funkciji
        std::cerr << "Error in select" << std::endl;
        return 0;
    } else if (ready == 0) {
        // Timeout je istekao, nema podataka za prijem
        std::cout << "Timeout expired, no data received" << std::endl;
        return 0;
    } else {
        // Podaci su primljeni, pokušaj prijema
        int bytes_received = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&server_addr, &server_len);
        if (bytes_received < 0) {
            // Prijem neuspeo
            std::cerr << "Receive confirmation failed" << std::endl;
            return 0;
        }
        // Prijem uspeo
        buffer[bytes_received] = '\0';
        std::string message(buffer);
        std::cout << "Received confirmation message from controller:" << std::endl;
        std::cout << message << std::endl;
        return 1;
    }
}


// Funkcija za slanje NOTIFY poruke tipa ssdp:alive
void send_notify(int sockfd, struct sockaddr_in server_addr, std::string id) {
    std::string notify = "NOTIFY * HTTP/1.1\r\n"
                         "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                         "NTS: ssdp:alive\r\n"
                         "USN: uuid:" + id + "\r\n"
                         "CACHE-CONTROL: max-age=1800\r\n"
                         "LOCATION: http://localhost/device\r\n"
                         "\r\n";

    sendto(sockfd, notify.c_str(), notify.length(), 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
}

// Funkcija za slanje NOTIFY poruke tipa ssdp:byebye
void send_byebye(int sockfd, struct sockaddr_in server_addr, std::string id) {
    std::string byebye = "NOTIFY * HTTP/1.1\r\n"
                         "HOST: " + std::string(multicast_address) + ":" + std::to_string(multicast_port) + "\r\n"
                         "NTS: ssdp:byebye\r\n"
                         "USN: uuid:" + id + "\r\n"
                         "\r\n";

    sendto(sockfd, byebye.c_str(), byebye.length(), 0,(struct sockaddr*)&server_addr, sizeof(server_addr));
}


int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[1024];
    
    std::string id = generate_unique_id();
    //std::string id  = "4878";

    // Pravljenje UDP socket-a
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }

    // Server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(multicast_address);
    server_addr.sin_port = htons(multicast_port);

    // Join multicast group
    ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(multicast_address);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) < 0) {
        std::cerr << "Joining multicast group failed" << std::endl;
        return 1;
    }
    
    // Slanje M-SEARCH poruke kontroleru
   bool controller_found = false; // Da li je kontroler pronadjen

    // Petlja za slanje M-SEARCH poruka dok se kontroler ne pojavi
    while (!controller_found) {
        // Slanje M-SEARCH poruke
        send_discovery(sockfd, server_addr);
        std::cout << "M-SEARCH sent." << std::endl;

        // Čekanje na odgovor
        bool flag_received = receive_confirmation_with_timeout(sockfd, server_addr, 5);
        //std::cout << flag_received << std:: endl; // Za debagovanje.

        if (flag_received) {
            controller_found = true; // Kontroler je ponadjen, izlazimo iz petlje
        } 

    }
    
    
    
    // Signal handler za Ctrl+C
    struct sigaction sig_int_handler;
    sig_int_handler.sa_handler = ctrl_c_handler;
    sigemptyset(&sig_int_handler.sa_mask);
    sig_int_handler.sa_flags = 0;
    sigaction(SIGINT, &sig_int_handler, nullptr);

    while (!ctrl_c_received) {

        // Slanje NOTIFY ssdp:alive poruke
        send_notify(sockfd, server_addr, id);
        std::cout << "NOTIFY sent.\n" << std::endl;
    	
        // Primanje RESPONSE poruke
        receive_confirmation(sockfd, server_addr);


        // Slanje informacija o uredjaju
        send_json_response(sockfd, server_addr, id);
        std::cout << "Device status sent.\n" << std::endl;
        std::cout << "************************************" << std::endl;

        // Sleep for 3 seconds
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    
    // Slanje NOTIFY ssdp::byebye poruke ako se ugasi uredljaj
    send_byebye(sockfd, server_addr, id);

    close(sockfd);
    return 0;
}

