#include "mbed.h"
#include "bbcar.h"
#include "Pixy2/Pixy2MbedSPI.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

// --- Hardware Initialization ---
Ticker servo_ticker;
Ticker servo_feedback_ticker;
PwmIn servo0_f(D9), servo1_f(D10);
PwmOut servo0_c(D11), servo1_c(D12);
BBCar car(servo0_c, servo0_f, servo1_c, servo1_f, servo_ticker, servo_feedback_ticker);

Pixy2MbedSPI pixy(PD_4, PD_3, PD_1, PD_5);

// --- Network Globals ---
NetworkInterface *net = nullptr;
MQTTNetwork *mqttNetwork = nullptr;
MQTT::Client<MQTTNetwork, Countdown> *client = nullptr;

const char* broker_ip = "192.168.50.176"; // MUST MATCH YOUR LAPTOP / BROKER IP
int broker_port = 1883;

// HARDCODED WIFI CREDENTIALS (Bypasses CMake JSON issues)
const char* WIFI_SSID = "ASUS_16";
const char* WIFI_PASS = "christopher";

// --- FSM & Global Variables ---
enum State {
    STATE_FOLLOW_LINE,
    STATE_HANDLE_BARCODE,
    STATE_SEEK_LINE,
    STATE_STOP
};

State current_state = STATE_FOLLOW_LINE;
int active_barcode = -1;
int barcode_cooldown_timer = 0; 
int last_turn_dir = 1;   

// Speed Constants
const float base_speed = 60.0f;
const float turn_speed = 45.0f;
const float wheel_speed_limit = 100.0f;

// PD Controller & Filter Variables
float prev_error = 0.0f;
float smoothed_error = 0.0f;
bool has_prev_error = false;

// --- Manual Override Variables ---
volatile bool auto_mode = true;       // True = PixyCam, False = MQTT Manual Control
volatile char manual_cmd = 'S';       // Default to Stop

// --- Physical Movement Helpers ---
void drive_forward_cm(int cm) {
    car.driveLR(base_speed, base_speed);
    ThisThread::sleep_for(std::chrono::milliseconds(cm * 40)); 
    car.stop();
}

void turn_degrees(int dir, int degrees) {
    car.driveLR(dir * turn_speed, -dir * turn_speed);
    ThisThread::sleep_for(std::chrono::milliseconds(degrees * 12)); 
    car.stop();
}

// --- MQTT Message Callback ---
void messageArrived(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char payload[32];
    int len = message.payloadlen < 31 ? message.payloadlen : 31;
    memcpy(payload, message.payload, len);
    payload[len] = '\0';

    printf("MQTT Command Received: %s\n", payload);

    if (strcmp(payload, "MODE_MANUAL") == 0) {
        auto_mode = false;
        manual_cmd = 'S'; // Safe default
        car.stop();
        printf(">>> SWITCHED TO MANUAL MODE <<<\n");
    } 
    else if (strcmp(payload, "MODE_AUTO") == 0) {
        auto_mode = true;
        current_state = STATE_FOLLOW_LINE;
        has_prev_error = false;
        smoothed_error = 0.0f;
        printf(">>> SWITCHED TO AUTO MODE <<<\n");
    } 
    else if (!auto_mode) {
        // If we are in manual mode, update the direction command
        manual_cmd = payload[0]; 
    }
}

// --- Network Thread ---
void mqtt_thread_func() {
    while(true) {
        if (client && client->isConnected()) {
            client->yield(20); // Process incoming messages
        }
        ThisThread::sleep_for(20ms); // Prevent thread starvation
    }
}

// --- Feature Functions ---
void line_follow() {
    int8_t res = pixy.line.getAllFeatures();
    
    if (res <= 0) {
        car.stop();
        has_prev_error = false; 
        smoothed_error = 0.0f; 
        return;
    }

    if (pixy.line.numBarcodes > 0 && barcode_cooldown_timer == 0) {
        int detected_code = pixy.line.barcodes[0].m_code;
        int y_position = pixy.line.barcodes[0].m_y; 

        if (detected_code >= 0 && detected_code <= 3) {
            if (y_position > 40) {
                active_barcode = detected_code;
                barcode_cooldown_timer = 150; 
                current_state = STATE_HANDLE_BARCODE;
                has_prev_error = false; 
                smoothed_error = 0.0f;
                return;
            } else {
                car.driveLR(base_speed, base_speed);
                return; 
            }
        }
    }

    if (pixy.line.numVectors > 0) {
        const Vector &v = pixy.line.vectors[0];
        float center_x = (pixy.frameWidth > 0) ? (pixy.frameWidth / 2.0f) : 39.5f;

        float target_x = (static_cast<float>(v.m_x0) + static_cast<float>(v.m_x1)) / 2.0f;
        float raw_error = center_x - target_x;

        if (abs(raw_error) < 5.0f) raw_error = 0.0f;

        smoothed_error = (0.6f * raw_error) + (0.4f * smoothed_error);

        float error_ratio = abs(smoothed_error) / 30.0f; 
        if (error_ratio > 1.0f) error_ratio = 1.0f;
        float current_base_speed = base_speed * (1.0f - error_ratio); 

        float kp = 2.0f;  
        float kd = 2.0f;  
        float heading_cmd = kp * smoothed_error;

        if (has_prev_error) {
            heading_cmd += kd * (smoothed_error - prev_error);
        }
        prev_error = smoothed_error;
        has_prev_error = true;

        if (heading_cmd > 60.0f) heading_cmd = 60.0f;
        if (heading_cmd < -60.0f) heading_cmd = -60.0f;

        float left = current_base_speed + heading_cmd;
        float right = current_base_speed - heading_cmd;
        
        left = car.clamp(left, wheel_speed_limit, -wheel_speed_limit);
        right = car.clamp(right, wheel_speed_limit, -wheel_speed_limit);
        
        car.driveLR(left, right);
    } else {
        car.stop(); 
        has_prev_error = false; 
        smoothed_error = 0.0f;
    }
}

void handle_barcode() {
    car.stop();
    ThisThread::sleep_for(300ms); 

    switch (active_barcode) {
        case 0: // Turn Left
            drive_forward_cm(6);      
            turn_degrees(-1, 40);      
            last_turn_dir = -1;        
            current_state = STATE_SEEK_LINE; 
            break;
            
        case 1: // Straight
            drive_forward_cm(12);     
            current_state = STATE_FOLLOW_LINE; 
            break;
            
        case 2: // Turn Right
            drive_forward_cm(6);      
            turn_degrees(1, 40);       
            last_turn_dir = 1;         
            current_state = STATE_SEEK_LINE; 
            break;
            
        case 3: // Stop
            turn_degrees(1, 450); 
            current_state = STATE_STOP;
            return;

        default: 
            drive_forward_cm(8); 
            current_state = STATE_FOLLOW_LINE;
            break;
    }
    active_barcode = -1;
}

void seek_line() {
    pixy.line.getAllFeatures();
    
    if (pixy.line.numVectors > 0) {
        const Vector &v = pixy.line.vectors[0];
        float target_x = (static_cast<float>(v.m_x0) + static_cast<float>(v.m_x1)) / 2.0f;
        
        if (target_x > 30.0f && target_x < 50.0f) {
            car.stop();
            has_prev_error = false;
            smoothed_error = 0.0f;
            current_state = STATE_FOLLOW_LINE;
            return;
        }
    }
    float seek_spd = turn_speed * 0.8f;
    car.driveLR(last_turn_dir * seek_spd, -last_turn_dir * seek_spd);
}

// --- Main Entry ---
int main() {
    printf("Initializing Pixy2...\r\n");
    if (pixy.init() < 0) {
        printf("Pixy2 Init Failed!\n");
    } else {
        pixy.setLamp(1, 1);
        pixy.changeProg("line");
    }
    
    printf("\n--- FAULT TOLERANT NETWORK INIT ---\n");
    WiFiInterface *wifi = WiFiInterface::get_default_instance();
    
    if (!wifi) {
        printf("ERROR: No WiFi module found on this board! Proceeding OFFLINE.\n");
    } else {
        printf("Attempting to connect to SSID: %s\n", WIFI_SSID);
        
        // Use the hardcoded strings here
        int ret = wifi->connect(WIFI_SSID, WIFI_PASS, NSAPI_SECURITY_WPA_WPA2);
        
        if (ret != 0) {
            printf(">>> WiFi connection failed! Error Code: %d <<<\n", ret);
            
            if (ret == NSAPI_ERROR_NO_CONNECTION) {
                printf("Reason: Cannot find SSID or signal is too weak.\n");
            } else if (ret == NSAPI_ERROR_AUTH_FAILURE) {
                printf("Reason: Incorrect Password.\n");
            } else if (ret == NSAPI_ERROR_DHCP_FAILURE) {
                printf("Reason: Connected to router, but failed to get an IP address.\n");
            }
            printf(">>> Proceeding to OFFLINE AUTONOMOUS MODE. <<<\n");
        } else {
            printf("WiFi Connected successfully!\n");
            
            // --- FIX: Mbed 6+ Requires a SocketAddress object ---
            SocketAddress ip_addr;
            wifi->get_ip_address(&ip_addr);
            printf("Car IP Address: %s\n", ip_addr.get_ip_address());
            printf("Car MAC Address: %s\n", wifi->get_mac_address());
            // ----------------------------------------------------
            
            net = wifi;
            mqttNetwork = new MQTTNetwork(net);
            client = new MQTT::Client<MQTTNetwork, Countdown>(*mqttNetwork);

            printf("Connecting to MQTT Broker at %s:%d...\n", broker_ip, broker_port);
            if (mqttNetwork->connect(broker_ip, broker_port) != 0) {
                printf(">>> Broker connection failed! Is your laptop firewall blocking port 1883? Proceeding OFFLINE. <<<\n");
            } else {
                MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
                data.MQTTVersion = 3;
                data.clientID.cstring = (char*)"BL_IOT_Car";

                if (client->connect(data) != 0) {
                    printf(">>> MQTT logic connect failed! Proceeding OFFLINE. <<<\n");
                } else {
                    printf(">>> MQTT Broker Connected! Ready for Remote Commands. <<<\n");
                    client->subscribe("car/control", MQTT::QOS0, messageArrived);
                }
            }
        }
    }
    printf("-----------------------------------\n\n");

    // Start network background thread (Safe even if offline)
    Thread mqtt_thread(osPriorityNormal, 2048);
    mqtt_thread.start(mqtt_thread_func);
    
    printf("FSM Started.\r\n");

    while (true) {
        if (auto_mode) {
            // ----- AUTO MODE: PixyCam FSM -----
            if (barcode_cooldown_timer > 0) barcode_cooldown_timer--;

            switch (current_state) {
                case STATE_FOLLOW_LINE:     line_follow(); break;
                case STATE_HANDLE_BARCODE:  handle_barcode(); break;
                case STATE_SEEK_LINE:       seek_line(); break;
                case STATE_STOP:            car.stop(); break;
            }
        } else {
            // ----- MANUAL MODE: Execute current MQTT Command -----
            switch(manual_cmd) {
                case 'F': car.driveLR(base_speed, base_speed); break;
                case 'B': car.driveLR(-base_speed, -base_speed); break;
                case 'L': car.driveLR(-turn_speed, turn_speed); break;
                case 'R': car.driveLR(turn_speed, -turn_speed); break;
                case 'S': 
                default:  car.stop(); break;
            }
        }
        
        ThisThread::sleep_for(20ms); // 50Hz unified control loop
    }
}