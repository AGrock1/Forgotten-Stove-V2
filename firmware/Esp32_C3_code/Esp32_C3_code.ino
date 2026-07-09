 
  // Libraries requirted
  #include <Wire.h>
  #include <Adafruit_AMG88xx.h>
  #include <HardwareSerial.h>
  #include <MyLD2410.h>

  // Constants
  const int max_distance = 150;
  const float hot_temp = 34.0;
  const int buzzer_pin = 3;
  const int green_led_pin = 4;
  const int red_led_pin = 5;

  // Objects and variables
  Adafruit_AMG88xx stove;
  MyLD2410 radar(Serial1);
  unsigned long lastThermalCheck = 0; // stopwatch

void setup() {

  // Setup serial monitor
  Serial.begin(115200);
  delay(2000);

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
