 
  // Libraries requirted
  #include <Wire.h>
  #include <Adafruit_AMG88xx.h>
  #include <HardwareSerial.h>
  #include <MyLD2410.h>
  #include <MFRC522.h>
  #include <SPI.h>
  #include <WiFiManager.h>
  #include <WiFi.h>

  // Constants
  const int max_distance = 150;
  const float hot_temp = 34.0;
  const int buzzer_pin = 3;
  const int green_led_pin = 5;
  const int red_led_pin = 4;

  #define rst_pin 2
  #define ss_pin 10

  // Objects and variables
  Adafruit_AMG88xx stove;
  MyLD2410 radar(Serial1);
  MFRC522 rfid(ss_pin,rst_pin);
  unsigned long lastThermalCheck = 0; // stopwatch

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

  // set pins to output mode
  pinMode(buzzer_pin, OUTPUT);
  pinMode(green_led_pin, OUTPUT);
  pinMode(red_led_pin, OUTPUT);

  // Turn on I2C and start thermal camera
  Wire.begin(8, 9);
  stove.begin();

  // Turn on Serial1 and start Radar
  Serial1.begin(256000, SERIAL_8N1, 20, 21);
  radar.begin();

  // Turn on SPI and start RFID Reader
  SPI.begin(6, 0, 7, 10); // SCK, MISO, MOSI, SS
  rfid.PCD_Init();
  Serial.println("RFID Scanner Online. Tap a card...");

}

void loop() {

  // Check for rfid tag
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()){
    Serial.print("User Tag Tapped! UID: ");
    String tagUID = "";

    // Read the 4 bytes of the card's UID and convert to Hexadecimal string
    for (byte i = 0; i < rfid.uid.size; i++) {
      tagUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
      tagUID += String(rfid.uid.uidByte[i], HEX);
    }
    
    tagUID.toUpperCase();
    Serial.println(tagUID);
    
    // Halt the card so it doesn't read the exact same tap 1000 times a second
    rfid.PICC_HaltA();
  }
  
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
      if (pixels[i] > max_temp){
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
      if (current_distance > 0 && current_distance <= max_distance){
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
    if (max_temp >= hot_temp){
      // Active cooking
      if (is_person_cooking == true){
        digitalWrite(green_led_pin, LOW);
        digitalWrite(red_led_pin, HIGH);
        digitalWrite(buzzer_pin, LOW);
      }
      // Forgotten stove
      else{
        digitalWrite(green_led_pin, LOW);
        digitalWrite(red_led_pin, HIGH);
        digitalWrite(buzzer_pin, HIGH);
      }

    }
    // Safe
    else{
      digitalWrite(green_led_pin, HIGH);
      digitalWrite(red_led_pin, LOW);
      digitalWrite(buzzer_pin, LOW);
    }
  }

}
