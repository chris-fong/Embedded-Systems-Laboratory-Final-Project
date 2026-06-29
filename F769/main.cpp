#include "mbed.h"
#include "lvgl.h"
#include "hal_stm_lvgl/tft/tft.h"
#include "hal_stm_lvgl/touchpad/touchpad.h"
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

// --- Network Globals ---
NetworkInterface *net;
MQTTNetwork *mqttNetwork;
MQTT::Client<MQTTNetwork, Countdown> *client;

const char* broker_ip = "192.168.50.176"; // DOUBLE CHECK YOUR LAPTOP IP!
int broker_port = 1883;

// --- Thread-Safe Command Handoff ---
volatile char pending_cmd = '\0';
volatile bool new_cmd_ready = false;

// --- Network Publisher (Runs ONLY in main loop) ---
void publish_cmd(const char* cmd) {
    if (!client || !client->isConnected()) return;
    
    MQTT::Message message;
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)cmd;
    message.payloadlen = strlen(cmd);
    
    client->publish("car/control", message);
    printf("MQTT Published: %s\n", cmd);
}

// --- LVGL Callbacks ---
static void dir_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    const char * cmd = (const char *)lv_event_get_user_data(e);

    if(code == LV_EVENT_PRESSED) {
        pending_cmd = cmd[0]; 
        new_cmd_ready = true;
    }
    else if(code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        pending_cmd = 'S'; 
        new_cmd_ready = true;
    }
}

static void mode_toggle_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        if(lv_obj_has_state(obj, LV_STATE_CHECKED)) {
            pending_cmd = 'M'; // Manual
        } else {
            pending_cmd = 'A'; // Auto
        }
        new_cmd_ready = true;
    }
}

// --- UI Builder ---
void build_ui() {
    lv_obj_t * sw = lv_switch_create(lv_scr_act());
    lv_obj_align(sw, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_add_event_cb(sw, mode_toggle_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * sw_label = lv_label_create(lv_scr_act());
    lv_label_set_text(sw_label, "Auto / Manual");
    lv_obj_align_to(sw_label, sw, LV_ALIGN_OUT_LEFT_MID, -10, 0);

    auto create_dir_btn = [](const char* symbol, const char* cmd, int x_ofs, int y_ofs) {
        lv_obj_t * btn = lv_btn_create(lv_scr_act());
        lv_obj_set_size(btn, 80, 80);
        lv_obj_align(btn, LV_ALIGN_CENTER, x_ofs, y_ofs);
        lv_obj_add_event_cb(btn, dir_btn_event_cb, LV_EVENT_ALL, (void*)cmd);

        lv_obj_t * label = lv_label_create(btn);
        lv_label_set_text(label, symbol);
        lv_obj_center(label);
    };

    create_dir_btn(LV_SYMBOL_UP, "F", 0, -100);
    create_dir_btn(LV_SYMBOL_DOWN, "B", 0, 100);
    create_dir_btn(LV_SYMBOL_LEFT, "L", -100, 0);
    create_dir_btn(LV_SYMBOL_RIGHT, "R", 100, 0);
}

// --- THE GOLDEN THREAD ---
// This runs parallel to everything else, ensuring the UI never freezes!
void lvgl_thread_func() {
    while (1) {
        lv_tick_inc(10);     // The missing heartbeat!
        lv_task_handler();   // The paint command!
        ThisThread::sleep_for(10ms);
    }
}

// --- Main Execution ---
int main(void) {
    printf("Initializing System...\n");

    // 1. Init UI
    lv_init();
    tft_init();
    touchpad_init();
    build_ui();

    // 2. Start UI Thread IMMEDIATELY
    // We give it a large memory stack (4096) to prevent LVGL from crashing
    Thread lvgl_thread(osPriorityNormal, 4096);
    lvgl_thread.start(lvgl_thread_func);
    printf("UI Render Thread Started!\n");

    // 3. Init Network
    // This will block the main thread, but the UI thread will keep drawing!
    printf("Connecting to Network...\n");
    net = NetworkInterface::get_default_instance();
    if (!net || net->connect() != 0) {
        printf("Network connection failed!\n");
        return -1;
    }
    printf("Network Connected.\n");

    // 4. Init MQTT
    mqttNetwork = new MQTTNetwork(net);
    client = new MQTT::Client<MQTTNetwork, Countdown>(*mqttNetwork);

    printf("Connecting to Broker %s...\n", broker_ip);
    int rc = mqttNetwork->connect(broker_ip, broker_port);
    if (rc != 0) {
        printf("Broker connection failed.\n");
        return -1;
    }

    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"F769_Remote";

    if ((rc = client->connect(data)) != 0) {
        printf("MQTT logic connect failed: %d\n", rc);
    } else {
        printf("MQTT Broker Connected successfully!\n");
    }

    // 5. Safe Single-Threaded Network Loop
    while (1) {
        if (new_cmd_ready) {
            new_cmd_ready = false;
            char cmd = pending_cmd;
            
            if (cmd == 'M') publish_cmd("MODE_MANUAL");
            else if (cmd == 'A') publish_cmd("MODE_AUTO");
            else {
                char str_cmd[2] = {cmd, '\0'};
                publish_cmd(str_cmd);
            }
        }
        
        client->yield(50); 
        ThisThread::sleep_for(50ms);
    }
}