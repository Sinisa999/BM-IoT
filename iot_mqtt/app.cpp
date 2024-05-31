#include <iostream>
#include <cpphttplib/httplib.h> // HTTP library for C++

int main() {
    // Create HTTP client
    httplib::Client client("http://localhost:8080");
    // Send HTTP GET request to retrieve current environment state
    auto res = client.Get("/environment");
    // Check if the request was successful
    if (res && res->status == 200) {
        // Print response body (environment state)
        std::cout << "Current Environment State:\n" << res->body << std::endl;
    } else {
        std::cerr << "Error: Unable to retrieve environment state.\n";
        return 1;
    }
    return 0;
}

