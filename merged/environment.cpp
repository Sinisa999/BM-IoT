#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <jsoncpp/json/json.h> // JSON library
#include <httplib.h> // HTTP library (make sure you have httplib.h)

using namespace std;

struct EnvironmentState {
    double temperature;
    double humidity;
    double light;
    double soil_ph;
    string relay_vent_state;
    string relay_pump_state;
};

// Function to simulate environment
void simulateEnvironment(EnvironmentState& state) {
    // Simulate environment parameters
    while (true) {
        if (state.relay_vent_state == "ON") {
            state.temperature -= 0.3; // Temperature decreases when vent is on
            cout << "usao sam u if" << endl;
        }
        else{
            state.temperature += 0.5;
        }
        if(state.relay_pump_state == "ON") {
            state.humidity += 0.8;
        }
        else{
            state.humidity -= 0.2;
        }
    

        state.light += 5000; // Adjust increment as needed
        state.soil_ph = 7.0; // Soil pH remains constant

        Json::Value root;
        root["temperature"] = state.temperature;
        root["humidity"] = state.humidity;
        root["light"] = state.light;
        root["soil_ph"] = state.soil_ph;
        root["relay_vent_state"] = state.relay_vent_state;
        root["relay_pump_state"] = state.relay_pump_state;

        std::ofstream file("environment.json");
        file << root;
        file.close();
        
        // Print the current state
        std::cout << "Temperature: " << state.temperature << " °C" <<std::endl;
        std::cout << "Humidity: " << state.humidity << " %" << std::endl;
        std::cout << "Light: " << state.light << " lx" << std::endl;
        std::cout << "pH: " << state.soil_ph << std::endl;
        std::cout << "Relay Vent State: " << state.relay_vent_state << std::endl;
        cout << "Relay Pump State: " << state.relay_pump_state << endl;
        std::cout << "*********************************" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3)); 
    }
}

void startHttpServer(EnvironmentState& state) {
    httplib::Server svr;

    svr.Get("/environment", [&state](const httplib::Request& req, httplib::Response& res) {
        Json::Value root;
        root["temperature"] = state.temperature;
        root["humidity"] = state.humidity;
        root["light"] = state.light;
        root["soil_ph"] = state.soil_ph;
        root["relay_vent_state"] = state.relay_vent_state;
        root["relay_pump_state"] = state.relay_pump_state;

        Json::StreamWriterBuilder writer;
        std::string output = Json::writeString(writer, root);
        res.set_content(output, "application/json");
    });

    svr.Post("/update_relay_state", [&state](const httplib::Request& req, httplib::Response& res) {
        //Stanje ventilatora
        if (req.has_param("vent")) {
            state.relay_vent_state = req.get_param_value("vent");
            res.set_content("Relay vent state updated", "text/plain");
        } else {
            res.set_content("Missing state parameter", "text/plain");
        }
        //Stanje pumpe
        if (req.has_param("pump")) {
            state.relay_pump_state = req.get_param_value("pump");
            res.set_content("Relay vent state updated", "text/plain");
        } else {
            res.set_content("Missing state parameter", "text/plain");
        }
    });

    svr.listen("0.0.0.0", 8080); // Ensure correct port
}

int main() {
    EnvironmentState environmentState;
    environmentState.temperature = 24.0; // Initial temperature in Celsius
    environmentState.humidity = 60.0; // Initial humidity percentage
    environmentState.light = 10000.0; // Initial light intensity in lux
    environmentState.soil_ph = 7.0; // Initial soil pH
    environmentState.relay_vent_state = "OFF"; // Initial state of relay vent
    environmentState.relay_pump_state = "OFF";

    std::thread simulation_thread(simulateEnvironment, std::ref(environmentState));
    startHttpServer(environmentState);
    
    simulation_thread.join();
    return 0;
}

