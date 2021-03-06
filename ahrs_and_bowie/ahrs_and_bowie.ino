#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_L3GD20_U.h>
#include <Adafruit_LSM303_U.h>
#include <MahonyAHRS.h>
#include <MadgwickAHRS.h>

#include <Servo.h>
#include <Streaming.h>
#include "Promulgate.h"
#include <XBee.h>


// ------- robot pins

int led = 13;
int led_green = 21;
int superbright_l = 20;
int superbright_r = 3;

// TB6612FNG motor test
#define MOTORA_SPEED 23
#define MOTORB_SPEED 22

#define MOTORA_CTRL2 17
#define MOTORA_CTRL1 16

#define MOTORB_CTRL1 12
#define MOTORB_CTRL2 11

#define MOTOR_DIR_FWD false
#define MOTOR_DIR_REV true

#define SONAR_LEFT A10
#define SONAR_RIGHT A11

#define SERVO_ARM 4
#define SERVO_CLAW 5
#define SERVO_ARM2 6

Servo arm;
Servo claw;
Servo arm2;

boolean claw_state = false;

int arm_max = 100; // standing up
int arm_home = 100; // standing up
int arm_min = 180; // closest to ground
int arm_exploration = 40; // all the way up, without the end effector on it

int claw_min = 1500;
int claw_home = 800;
int claw_max = 500;


// ------------


// ----------- promulgate

boolean DEBUG = false;
Promulgate promulgate = Promulgate(&Serial2, &Serial2);

void transmit_complete();
void received_action(char action, char cmd, uint8_t key, uint16_t val, char delim);

long last_rx = 0;

// -------


// ------------- Xbee vars
XBee xbee = XBee();

XBeeAddress64 addr64 = XBeeAddress64(0x00000000, 0x0000ffff);
XBeeAddress64 addr_controller = XBeeAddress64(0x0013A200, 0x40DD9902);
XBeeAddress64 addr_robot = XBeeAddress64(0x0013A200, 0x40D96FC2);
ZBTxStatusResponse txStatus = ZBTxStatusResponse();
ZBRxResponse rx = ZBRxResponse();
char message_tx[64];
char message_rx[64];
uint32_t msg_tx_count = 0;
uint32_t msg_rx_count = 0;
uint32_t msg_tx_err = 0;

// -------------


// ---------- ahrs

// Note: This sketch requires the MahonyAHRS sketch from
// https://github.com/PaulStoffregen/MahonyAHRS, or the
// MagdwickAHRS sketch from https://github.com/PaulStoffregen/MadgwickAHRS

// Note: This sketch is a WORK IN PROGRESS

// Create sensor instances.
Adafruit_L3GD20_Unified       gyro(20);
Adafruit_LSM303_Accel_Unified accel(30301);
Adafruit_LSM303_Mag_Unified   mag(30302);

// Mag calibration values are calculated via ahrs_calibration.
// These values must be determined for each baord/environment.
// See the image in this sketch folder for the values used
// below.

// Offsets applied to raw x/y/z values
//float mag_offsets[3]            = { -2.20F, -5.53F, -26.34F };
float mag_offsets[3]            = { 0.0F, 0.0F, 0.0F };

// Soft iron error compensation matrix
float mag_softiron_matrix[3][3] = { { 0.934, 0.005, 0.013 },
                                    { 0.005, 0.948, 0.012 },
                                    { 0.013, 0.012, 1.129 } }; 
//float mag_softiron_matrix[3][3] = { { 0.0, 0.0, 0.0 },
//                                    { 0.0, 0.0, 0.0 },
//                                    { 0.0, 0.0, 0.0 } }; 


float mag_field_strength        = 48.41F;

// Mahony is lighter weight as a filter and should be used
// on slower systems
//Mahony filter;
Madgwick filter;


// averaging filter
#define READ_SAMPLES 50
#define PREV_WEIGHT 0.4
int num_sample = 0;
float heading_reading[2];
float heading_total = 0;
float heading_result = 0;

// go to a target
#define DESTINATION_THRESH 5
#define STRAIGHT_ANGLE_THRESH 30.0
#define COMPASS_MOTOR_PAUSE 3000
float MOTOR_HEADING_ON_TIME = 100;
float target = 48;
float alpha = 0;
bool turning = false;
bool clockwise = false;
long last_heading_change = 0;
long last_pause_time = 0;
boolean pause_for_compass = true;
// -------------------


// ------- state machine

int REMOTE_OP_STATE = 1;
int HEADING_STATE = 2;
int GPS_STATE = 3;
int TESTING_STATE = 4;
int MAKERFAIREOTT = 5;
int AHRS_TESTING = 6;

int STATE = AHRS_TESTING;

boolean AUTONOMOUS = true;
boolean EMERGENCY_STOP = false;
boolean FOLLOW_HEADING = true;
boolean FOLLOW_GPS = true;
boolean GOTO_GPS_WAYPOINT = true;
boolean ULTRASONIC_MOTOR_OVERRIDE = false;

// --------

// ------ obstacle detection

#define ULTRASONIC_SAMPLES 30
#define PREV_ULTRASONIC_WEIGHT 0.4
#define ULTRASONIC_THRESH_STOP 40
#define ULTRASONIC_THRESH_SLOW 50
int ultrasonic_sample = 0;
float ultrasonic_left_reading[2];
float ultrasonic_right_reading[2];
float ultrasonic_left_total = 0;
float ultrasonic_right_total = 0;
float ultrasonic_left_result = 0;
float ultrasonic_right_result = 0;
float ultrasonic_left_cm = 0;
float ultrasonic_right_cm = 0;

#define TRIG_COUNT 20
int left_trig_count[2] = {0,0};
long last_left_trig = 0;
int right_trig_count[2] = {0,0};
long last_right_trig = 0;
bool left_stop = false;
bool left_slow = false;
bool right_stop = false;
bool right_slow = false;

#define SLOW_SPEED 0.4
float SPEED_ADJUST = 1.0;
// --------

// --------- GPS
#define GOAL_THRESH 3.0

String lat_buf = "";
String lon_buf = "";
float lat_current = 0.0;
float lon_current = 0.0;
int reading_state = 0;
long last_gps_receive = 0;
uint32_t gps_receives = 0;

// cherry beach - super field test 2016
//#define GOAL_LAT 43.636806
//#define GOAL_LON -79.343826

// home plate
#define GOAL_LAT 44.215789
#define GOAL_LON -76.524040
#define COMPASS_DELTA 239.0
float goal_distance = 0.0;
float goal_heading = 0.0;
// -----------


long current_time = 0;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600); // promulgate via xbee
  Serial3.begin(9600); // gps
  xbee.setSerial(Serial2);

  promulgate.LOG_LEVEL = Promulgate::ERROR_;
  promulgate.set_rx_callback(received_action);
  promulgate.set_tx_callback(transmit_complete);

  Serial.print(F("Hello! I am Bowie!\n"));

  pinMode(SONAR_RIGHT, INPUT);
  pinMode(SONAR_LEFT, INPUT);

  pinMode(superbright_l, OUTPUT);
  pinMode(superbright_r, OUTPUT);
  
  pinMode(led_green, OUTPUT);
  digitalWrite(led_green, HIGH);

  arm.attach(SERVO_ARM);
  claw.attach(SERVO_CLAW);
  arm2.attach(SERVO_ARM2);

  arm.write(180-arm_exploration);
  arm2.write(arm_exploration);
  claw.writeMicroseconds(claw_home);

  pinMode(led, OUTPUT);
  digitalWrite(led, HIGH);
  
  motor_init();

  initSensors();

  lat_buf.reserve(32);
  lon_buf.reserve(32);
  
  Serial.print(F("Let's go! Nom nom nom"));

//  while(1<3) {
//    forward(0, 255);
//    forward(1, 255);
//    delay(500);
//    emergencyStop();
//    delay(1000);
//  }

}

void loop(void) {

  current_time = millis();

  //Serial << "!" << endl;

  // --------- read ultrasonic sensors

  if(ultrasonic_sample < ULTRASONIC_SAMPLES) {
    ultrasonic_left_reading[1] = ultrasonic_left_reading[0];
    ultrasonic_left_reading[0] = analogRead(SONAR_LEFT) / 4;// * (1023/3.3);
    ultrasonic_left_total += ( ultrasonic_left_reading[0] + PREV_ULTRASONIC_WEIGHT * ultrasonic_left_reading[1] );

    ultrasonic_right_reading[1] = ultrasonic_right_reading[0];
    ultrasonic_right_reading[0] = analogRead(SONAR_RIGHT) / 4;// * (1023/3.3);
    ultrasonic_right_total += ( ultrasonic_right_reading[0] + PREV_ULTRASONIC_WEIGHT * ultrasonic_right_reading[1] );

    ultrasonic_sample++;
  } else {

    ultrasonic_left_result = ultrasonic_left_total / ULTRASONIC_SAMPLES;
    ultrasonic_right_result = ultrasonic_right_total / ULTRASONIC_SAMPLES;

    ultrasonic_left_cm = ultrasonic_left_result * 2.54;
    ultrasonic_right_cm = ultrasonic_right_result * 2.54;

    ultrasonic_sample = 0;
    ultrasonic_left_total = 0;
    ultrasonic_right_total = 0;

    //Serial << millis() << "\tL: " << ultrasonic_left_result << "\tR: " << ultrasonic_right_result;
    //Serial << "\tL (cm): " << ultrasonic_left_cm << "\tR (cm): " << ultrasonic_right_cm << endl;
  }
  
  // ----------

  // ----------- obstacle detection

  if(ultrasonic_left_cm < ULTRASONIC_THRESH_STOP) {
    left_trig_count[0]++;
    last_left_trig = current_time;
    //Serial << current_time << "\tleft stop \t" << left_trig_count[0] << endl;
  } else if(ultrasonic_left_cm < ULTRASONIC_THRESH_SLOW) {
    left_trig_count[1]++;
    last_left_trig = current_time;
    //Serial << current_time << "\tleft slow" << endl;
  }

  if(ultrasonic_right_cm < ULTRASONIC_THRESH_STOP) {
    right_trig_count[0]++;
    last_right_trig = current_time;
    //Serial << current_time << "\tright stop" << endl;
  } else if(ultrasonic_right_cm < ULTRASONIC_THRESH_SLOW) {
    right_trig_count[1]++;
    last_right_trig = current_time;
    //Serial << current_time << "\tright slow" << endl;
  }

  if(current_time-last_left_trig >= 500) {
    //Serial << current_time << "\tleft trig reset" << endl;
    last_left_trig = 0;
    left_trig_count[0] = 0;
    left_trig_count[1] = 0;
    SPEED_ADJUST = 1.0;
  }

  if(current_time-last_right_trig >= 500) {
    //Serial << current_time << "\tright trig reset" << endl;
    last_right_trig = 0;
    right_trig_count[0] = 0;
    right_trig_count[1] = 0;
    SPEED_ADJUST = 1.0;
  }

  // -----------

  // ---------- action

  if(ULTRASONIC_MOTOR_OVERRIDE) {

    if(!GOTO_GPS_WAYPOINT) { // if we aren't going to a gps waypoint, then adjust based off of sensors

      if(left_trig_count[0] >= TRIG_COUNT || left_trig_count[1] >= TRIG_COUNT) {
        if(left_trig_count[0] > left_trig_count[1]) { // stop
          Serial << current_time << "\tleft ultrasonic override stop" << endl;
          SPEED_ADJUST = 0.0;
        } else { // slow down
          Serial << current_time << "\tleft ultrasonic override slow down" << endl;
          SPEED_ADJUST = SLOW_SPEED;
        }
      }
    
      if(right_trig_count[0] >= TRIG_COUNT || right_trig_count[1] >= TRIG_COUNT) {
        if(right_trig_count[0] > right_trig_count[1]) { // stop
          Serial << current_time << "\tright ultrasonic override stop" << endl;
          SPEED_ADJUST = 0.0;
        } else { // slow down
          Serial << current_time << "\tright ultrasonic override slow down" << endl;
          SPEED_ADJUST = SLOW_SPEED;
        }
      }
      
    } else { // if we are following a heading, try to move around it

      // TODO: Debug this in the field
//      
//        // backup
//        if(left_trig_count[0] >= TRIG_COUNT || left_trig_count[1] >= TRIG_COUNT) {
//          if(right_trig_count[0] >= TRIG_COUNT || right_trig_count[1] >= TRIG_COUNT) {
//            reverse(0, 255);
//            reverse(1, 255);
//            delay(1000);
//          }
//        }
//  
//        // turn right
//        if(left_trig_count[0] >= TRIG_COUNT || left_trig_count[1] >= TRIG_COUNT) {
//          reverse(0, 150);
//          forward(1, 255);
//          delay(500);
//        }
//  
//        // turn left
//        if(right_trig_count[0] >= TRIG_COUNT || right_trig_count[1] >= TRIG_COUNT) {
//          forward(0, 255);
//          reverse(1, 150);
//          delay(500);
//        }
      
      
    }

  }

  // ----------

  

  // --------- ahrs code
  
  sensors_event_t gyro_event;
  sensors_event_t accel_event;
  sensors_event_t mag_event;

  // Get new data samples
  gyro.getEvent(&gyro_event);
  accel.getEvent(&accel_event);
  mag.getEvent(&mag_event);

  // Apply mag offset compensation (base values in uTesla)
  float x = mag_event.magnetic.x - mag_offsets[0];
  float y = mag_event.magnetic.y - mag_offsets[1];
  float z = mag_event.magnetic.z - mag_offsets[2];

  // Apply mag soft iron error compensation
  float mx = x * mag_softiron_matrix[0][0] + y * mag_softiron_matrix[0][1] + z * mag_softiron_matrix[0][2];
  float my = x * mag_softiron_matrix[1][0] + y * mag_softiron_matrix[1][1] + z * mag_softiron_matrix[1][2];
  float mz = x * mag_softiron_matrix[2][0] + y * mag_softiron_matrix[2][1] + z * mag_softiron_matrix[2][2];

  // The filter library expects gyro data in degrees/s, but adafruit sensor
  // uses rad/s so we need to convert them first (or adapt the filter lib
  // where they are being converted)
  float gx = gyro_event.gyro.x * 57.2958F;
  float gy = gyro_event.gyro.y * 57.2958F;
  float gz = gyro_event.gyro.z * 57.2958F;

  // Update the filter
  filter.update(gx, gy, gz,
                accel_event.acceleration.x, accel_event.acceleration.y, accel_event.acceleration.z,
                mx, my, mz);

  // Print the orientation filter output
  float roll = filter.getRoll();
  float pitch = filter.getPitch();
  float heading = filter.getYaw();

  // ---------


  // ------- filter it

  heading_reading[1] = heading_reading[0];
  heading_reading[0] = heading;

  if(num_sample < READ_SAMPLES) {

    heading_total += PREV_WEIGHT * heading_reading[1] + (1-PREV_WEIGHT) * heading_reading[0];
    
    num_sample++;
  } else {

    heading_result = heading_total / (READ_SAMPLES);

    // Serial.print(millis());
    // Serial.print("\t\t");
    // Serial.print(heading_result);
    // Serial.print("\n");

    heading_total = 0;
    num_sample = 0;
  }

  // --------


  // ------- go to a target

  if(current_time > 5000) { // ensure the program has been running for a bit

    float opp_angle;
    float current = heading_result;

    alpha = target - current;
    if(alpha > 180) alpha -= 360;
    if(alpha < -180)  alpha += 360;

    if(alpha < 0) {
      clockwise = true;
    } else {
      clockwise = false;
    }
    
//    Serial.print(millis());
//    Serial.print("\tH: ");
//    Serial.print(heading_result);
//    Serial.print("\tA: ");
//    Serial.print(alpha);
//    Serial.print("\n");

    if(FOLLOW_HEADING == true) {

     if(current_time-last_heading_change >= MOTOR_HEADING_ON_TIME) {

        if(pause_for_compass) {

          if(current_time-last_pause_time >= COMPASS_MOTOR_PAUSE) {
            last_pause_time = current_time;
            pause_for_compass = !pause_for_compass;
          } else {
            forward(0, 0);
            forward(1, 0);
            //Serial << "waiting... " << current_time-last_pause_time-1500 << endl;
          }
          
        } else {

          if(FOLLOW_GPS) {

            forward(0, 255);
            forward(1, 255);
            MOTOR_HEADING_ON_TIME = 2000;
            
          } else {
    
            if(abs(alpha) < STRAIGHT_ANGLE_THRESH) {
              //Serial.print("S ");
              forward(0, 255);
              forward(1, 255);
              MOTOR_HEADING_ON_TIME = 2000;
            } else {
              if(clockwise) {
                //Serial.print("CW ");
                reverse(0, 150);
                forward(1, 255);
                MOTOR_HEADING_ON_TIME = 250; // 100 for concrete, 250 for grass or sand
              } else {
                //Serial.print("CCW ");
                forward(0, 255);
                reverse(1, 150);
                MOTOR_HEADING_ON_TIME = 250;
              }
            }

          }
  
            pause_for_compass = !pause_for_compass;
            last_heading_change = current_time;

          
        }

     }
    }
    //Serial.print("\n");

  }

  // --------
  

  // -------- read from GPS
  //delay(100);
  while(Serial3.available()) {
    char c = Serial3.read();
    Serial << c;
    
    if(c == 'A') {
      //Serial << " a ";
      reading_state = 1;
    } else if(c == 'B') {
      //Serial << " b ";
      reading_state = 2;
    } else if(c == ',') {
      //Serial << " blorp ";
      reading_state = 0;
    } else if(c == ';') {
      //Serial << " ting ";
      reading_state = 0;
      lat_current = lat_buf.toFloat();
      lon_current = lon_buf.toFloat();
      lat_buf = "";
      lon_buf = "";
      gps_receives++;

      Serial << current_time << "\t" << gps_receives << "\tGPS lat: " << lat_current << "\tlon: " << lon_current << endl;

    }

    if(reading_state == 1) {
      if(c != 'A' && c != ',') lat_buf += c;
    } else if(reading_state == 2) {
      if(c != 'B' && c != ';') lon_buf += c;
    }

    last_gps_receive = current_time;
  
  }

  if(current_time-last_gps_receive >= 5000 && last_gps_receive != 0) {
    Serial << current_time << "\t!!! Haven't received anything from GPS in >5s" << endl;
    gps_receives = 0;
  }

  // ---------

  // --------- GPS action
  //delay(100);
  if(GOTO_GPS_WAYPOINT) {
        
    if(gps_receives > 5) {

      goal_distance = distanceBetween(lat_current, lon_current, GOAL_LAT, GOAL_LON);
      goal_heading = courseTo(lat_current, lon_current, GOAL_LAT, GOAL_LON);

      if(goal_distance > GOAL_THRESH) {
        // GO GO GO!!

        target = goal_heading-COMPASS_DELTA;
        
        Serial << "following the gps, target heading = " << target << endl;

        Serial << current_time << "\tDistance to goal: " << goal_distance;
        Serial << "\tCourse to: " << goal_heading;
        Serial << "\tCurrent Heading: " << heading_result << endl;
        
        FOLLOW_GPS = true;
        FOLLOW_HEADING = true;
      } else {
        Serial.print("Bowie has arrived at the coordinates!");
        FOLLOW_HEADING = false;
        FOLLOW_GPS = false;
        forward(0, 0);
        forward(1, 0);
        for(int i=0; i<2; i++) {
          digitalWrite(superbright_l, HIGH);
          digitalWrite(superbright_r, HIGH);
          delay(100);
          digitalWrite(superbright_l, LOW);
          digitalWrite(superbright_r, LOW);
          delay(100);
        }
      }
    }
    
  }

  // -----------


  // ------- remote operator

  
  if(xbeeRead()) {
    for(int i=0; i<rx.getDataLength(); i++) {
      promulgate.organize_message(message_rx[i]);
      Serial << message_rx[i];
      if(message_rx[i] == '!') Serial << "\n";
    }
  }
  
  if(current_time-last_rx >= 500) {
    digitalWrite(led_green, LOW);
    if(!AUTONOMOUS) { EMERGENCY_STOP = true; // if we lose connection, stop the robot
      EMERGENCY_STOP = true;
      forward(0, 0);
      forward(1, 0);
    }
  } else {
    digitalWrite(led_green, HIGH);
    if(!AUTONOMOUS) EMERGENCY_STOP = false;
  }
  

  // ---------------

  

  
}


void initSensors() {

  
  Serial.println(F("Adafruit 10 DOF Board AHRS Calibration Example")); Serial.println("");
  
  // Initialize the sensors.
  if(!gyro.begin())
  {
    /* There was a problem detecting the L3GD20 ... check your connections */
    Serial.println("Ooops, no L3GD20 detected ... Check your wiring!");
    while(1);
  }
  
  if(!accel.begin())
  {
    /* There was a problem detecting the LSM303DLHC ... check your connections */
    Serial.println("Ooops, no L3M303DLHC accel detected ... Check your wiring!");
    while(1);
  }
  
  if(!mag.begin())
  {
    /* There was a problem detecting the LSM303DLHC ... check your connections */
    Serial.println("Ooops, no L3M303DLHC mag detected ... Check your wiring!");
    while(1);
  }

  // Filter expects 50 samples per second
  filter.begin(50);
  
}

void printOrientation() {
  // Serial.print(millis());
  // Serial.print(" - Orientation: ");
  // Serial.print(heading);
  // Serial.print(" ");
  // Serial.print(pitch);
  // Serial.print(" ");
  // Serial.println(roll);
}





// -------- motors




void motor_init() {
  pinMode(MOTORA_CTRL1, OUTPUT);
  digitalWrite(MOTORA_CTRL1, LOW);
  pinMode(MOTORA_CTRL2, OUTPUT);
  digitalWrite(MOTORA_CTRL2, LOW);
  pinMode(MOTORA_SPEED, OUTPUT);
  analogWrite(MOTORA_SPEED, 0);

  pinMode(MOTORB_CTRL1, OUTPUT);
  digitalWrite(MOTORB_CTRL1, LOW);
  pinMode(MOTORB_CTRL2, OUTPUT);
  digitalWrite(MOTORB_CTRL2, LOW);
  pinMode(MOTORB_SPEED, OUTPUT);
  analogWrite(MOTORB_SPEED, 0);
}

void motor_setDir(uint8_t motorNum, bool dir) {
  if(0 == motorNum) {
    if(dir) {
      digitalWrite(MOTORA_CTRL1, HIGH);
      digitalWrite(MOTORA_CTRL2, LOW);        
    } else {
      digitalWrite(MOTORA_CTRL1, LOW);
      digitalWrite(MOTORA_CTRL2, HIGH);
    }
  } else if(1 == motorNum) {
    if(dir) {
      digitalWrite(MOTORB_CTRL2, HIGH);
      digitalWrite(MOTORB_CTRL1, LOW);    
    } else {
      digitalWrite(MOTORB_CTRL2, LOW);
      digitalWrite(MOTORB_CTRL1, HIGH);    
    }    
  }
}

void motor_setSpeed(uint8_t motorNum, uint8_t speed) {
  if(0 == motorNum) {
    analogWrite(MOTORA_SPEED, speed);
  } else if(1 == motorNum) {
    analogWrite(MOTORB_SPEED, speed);
  }
}

void motor_setBrake(uint8_t motorNum) {
  motor_setSpeed(motorNum, 0);
}

void motor_setCoast(uint8_t motorNum) {
  if(0 == motorNum) {
    digitalWrite(MOTORA_CTRL1, LOW);
    digitalWrite(MOTORA_CTRL2, LOW);
    digitalWrite(MOTORA_SPEED, HIGH);
  } else if(1 == motorNum) {
    digitalWrite(MOTORB_CTRL1, LOW);
    digitalWrite(MOTORB_CTRL2, LOW);
    digitalWrite(MOTORB_SPEED, HIGH);
  }
}


// -----------







