 
  // Libraries requirted
  #include <Wire.h>
  #include <Adafruit_AMG88xx.h>
  #include <HardwareSerial.h>
  #include <MyLD2410.h>
  #include <WiFiManager.h>
  #include <WiFi.h>
  #include <Firebase_ESP_Client.h>
  #include <addons/TokenHelper.h>

  // Firebase Credentials
  #include "password.h"

  // Constants
  const float hot_temp = 34.0;
  const int buzzer_pin = 3;
  const int green_led_pin = 5;
  const int red_led_pin = 4;

  // Objects and variables
  Adafruit_AMG88xx stove;
  MyLD2410 radar(Serial1);
  unsigned long lastThermalCheck = 0;
  unsigned long grace_time = 120000;
  unsigned long abandon_time = 0;
  unsigned long lastSettingsCheck = 0;
  int max_distance = 150;
  FirebaseData fbdo;
  FirebaseData fbdo_read;
  FirebaseAuth auth;
  FirebaseConfig config;
  String device_path = "/stove";

void setup() {

  // Setup serial monitor
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n--- BOOTING FORGOTTEN STOVE IOT ---");

  //Connecting to the wifi
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_5dBm); // Prevents the voltage regulator from crashing

  WiFiManager wm;
  
  // Enforce low power even when it switches to Access Point mode
  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("Portal Started! Re-enforcing low power whisper mode...");
    WiFi.setTxPower(WIFI_POWER_5dBm); 
  });

  Serial.println("Starting WiFiManager. Broadcasting 'Stove_Setup_Whisper' if no saved networks...");
  
  // Attempt to connect. If it fails, broadcast the setup portal.
  if (!wm.autoConnect("Stove_Setup_Whisper")) {
    Serial.println("Failed to connect or hit timeout. Resetting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("\nSUCCESS: Connected to external router!");
  Serial.print("Local IP Address: ");
  Serial.println(WiFi.localIP());

  // creating uniquw device path using MAC address

  String mac = WiFi.macAddress();
  mac.replace(":", ""); 
  device_path = "/devices/" + mac;
  Serial.print("Device Cloud Path: ");
  Serial.println(device_path);

  // Firebase setup and initialization

  Serial.printf("Connecting to Firebase Database: %s\n", firebase_host);
  config.database_url = firebase_host;
  config.signer.tokens.legacy_token = firebase_auth;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  fbdo.setBSSLBufferSize(2048, 1024);
  fbdo_read.setBSSLBufferSize(2048, 1024);
  Serial.println("Firebase Initialized!");

  // set pins to output mode
  pinMode(buzzer_pin, OUTPUT);
  pinMode(green_led_pin, OUTPUT);
  pinMode(red_led_pin, OUTPUT);

  // Turn on I2C and start thermal camera
  Wire.begin(8, 9);
  Wire.setClock(10000);
  if (!stove.begin()) {
    Serial.println("COULD NOT FIND AMG88xx THERMAL CAMERA! Check I2C wiring.");
  } 
  else {
    Serial.println("AMG88xx Thermal Camera Online!");
  }

  // Turn on Serial1 and start Radar
  Serial1.begin(256000, SERIAL_8N1, 20, 21);
  radar.begin();

}

void loop() {
  
  // Constantly check radar
  radar.check();

  // IF 1 second has passed:
  if (millis() - lastThermalCheck > 1000) {
    lastThermalCheck = millis(); // Reset the timer

    // Check temperature
    float pixels[64];
    stove.readPixels(pixels);

    float max_temp = 0.0;
    for (int i = 0; i < 64; i++){
      if (pixels[i] > max_temp && pixels[i] <= 100.0){
        max_temp = pixels[i];
      }
    }

    // Check person
    bool is_person_cooking = false; // initially assume no human present
    int current_distance = 0;

    if (radar.presenceDetected()){ // if person detected

      // Check distance to eliminate ghost reading
      if (radar.movingTargetDetected()){
        current_distance = radar.movingTargetDistance();
      }
      else if (radar.stationaryTargetDetected()){
        current_distance = radar.stationaryTargetDistance();
      }
      if (current_distance > 40 && current_distance <= max_distance){
        is_person_cooking = true;
      }

    }

    // printing to serial monitor for debugging
    Serial.print("Max Temp: ");
    Serial.print(max_temp);
    Serial.print(" C  |  Distance: ");
    Serial.print(current_distance);
    Serial.print(" cm  |  Cooking: ");
    Serial.println(is_person_cooking ? "YES" : "NO");

    // Apply safety rules
    bool alarm_triggered = false;
    long time_left_sec = grace_time / 1000;

    if (max_temp >= hot_temp){
      // Active cooking
      if (is_person_cooking == true){
        digitalWrite(green_led_pin, LOW);
        digitalWrite(red_led_pin, HIGH);
        digitalWrite(buzzer_pin, LOW);
        abandon_time = 0;
      }
      // Forgotten stove
      else{
        if (abandon_time == 0){
          abandon_time = millis();
        }
        digitalWrite(green_led_pin, LOW);
        digitalWrite(red_led_pin, HIGH);
        
        long elapsed_time = millis() - abandon_time;
        
        if (elapsed_time > grace_time){
          digitalWrite(buzzer_pin, HIGH);
          alarm_triggered = true;
          time_left_sec = 0;
        }
        else{
          digitalWrite(buzzer_pin, LOW);
          time_left_sec = (grace_time - elapsed_time) / 1000;
        }
      }

    }
    // Safe
    else{
      digitalWrite(green_led_pin, HIGH);
      digitalWrite(red_led_pin, LOW);
      digitalWrite(buzzer_pin, LOW);
      abandon_time = 0;
    }

    // updating temp
    FirebaseJson json;
    json.set("max_temp", max_temp);
    json.set("distance", current_distance);
    json.set("is_cooking", is_person_cooking);
    json.set("time_left", time_left_sec);           
    json.set("alarm_triggered", alarm_triggered);

    if (!Firebase.RTDB.setJSONAsync(&fbdo, device_path, &json)) {
      Serial.println("Failed to push JSON: " + fbdo.errorReason());
    }

    if (millis() - lastSettingsCheck > 10000) {
        lastSettingsCheck = millis();

      if (Firebase.RTDB.getString(&fbdo_read, device_path + "/grace_period")) {
        String user_pref = fbdo_read.stringData();
        user_pref.trim();
        user_pref.toLowerCase();

        if (user_pref == "short") {
          grace_time = 30000;  // 30 seconds
        } else if (user_pref == "medium") {
          grace_time = 120000; // 2 minutes
        } else if (user_pref == "long") {
          grace_time = 300000; // 5 minutes
        }
      }
      // Adjust radar sensitivity 
      if (Firebase.RTDB.getString(&fbdo_read, device_path + "/radar_sensitivity")) {
        String sensitivity = fbdo_read.stringData();
        sensitivity.trim();
        sensitivity.toLowerCase();

        if (sensitivity == "low") {
          max_distance = 100; // Requires user to be within 1 meter
        } else if (sensitivity == "medium") {
          max_distance = 150; // Requires user to be within 1.5 meters
        } else if (sensitivity == "high") {
          max_distance = 250; // Detects user up to 2.5 meters away
        }
      }
    }
    

  }

}
