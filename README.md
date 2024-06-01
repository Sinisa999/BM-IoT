# BM-IoT

# Setup:
● sudo apt-get update

● sudo apt-get intall libmosquitto-dev


● sudo apt install mosquitto-clients

● sudo systemctl start mosquitto

● sudo systemctl enable mosquitto

● sudo systemctl status mosquitto


● sudo apt-get update

● sudo apt-get install libjsoncpp-dev


# Build:

● envionment -> g++ -I. -o environment environment.cpp -ljsoncpp -pthread

● controller -> g++ -o controller controller.cpp -ljsoncpp -lmosquitto

● sensor -> g++ -I. -o sensor sensor.cpp -ljsoncpp -lmosquitto -pthread

● actuator -> g++ -I. -o actuator actuator.cpp -ljsoncpp -lmosquitto -pthread
