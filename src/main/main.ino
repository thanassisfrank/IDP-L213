// main.cpp
// the main program file for the arduino
#include <Servo.h>
#include <Adafruit_MotorShield.h>
#include <WiFiNINA.h>
#include "logger.h"
#include "movement.h"


// pin for each component =======================================================================
// digital pins
// port 2 is borked
const int oLedPin =         5;
const int rLedPin =         3;
const int gLedPin =         4;
const int servo1Pin =       9;
const int servo2Pin =      10;
// analogue pins
// line sensor
const int os1Pin =          0;  // analogue pin #
const int os2Pin =          1;  // analogue pin #
const int os3Pin =          2;  // analogue pin #
// colour sensor
const int bLDRPin =        A0;  // Blue colour LDR voltage (goes down with more light)
const int rLDRPin =        A1;  // Red colour LDR voltage
const int os4Pin =         A2;  // OPB704 Voltage (goes down with decreasing distance)
const int wLedPin =         9;  // Analog output pin that the LED is attached to
// ==============================================================================================
const int fSpeed =        200; // motor speed for general movement
// ==============================================================================================

enum Dir {LEFT, RIGHT};

enum Color {BLUE, RED};

bool MOTORSREVERSED = false;

//struct Motor {
//  int pin;
//  int currSpeed;
//};

struct Sensor {
  int pin;
  int value;
};

struct Led {
  int pin;
  bool state;
};

Logger l((unsigned long)500, BOTH);

Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *m1 = AFMS.getMotor(1);
Adafruit_DCMotor *m2 = AFMS.getMotor(2);

Servo servo;

// create led structs
Led oLed;
Led rLed;
Led gLed;
Led wLed;

//create sensor structs 
//optoswitches
Sensor os1;
Sensor os2;
Sensor os3;
Sensor os4;
//LDRs
Sensor rLDR;
Sensor bLDR;


// global flag to keep track of if the motors are running to know when to flash oLed
bool motorsActive = false;
//int currSpeed = 0; // keep a track of the current speed

// create object for handling the different movement regimes
// handles line following
Movement::FollowLine lineFollower(fSpeed, 70, (unsigned long)100);
// handles straight
Movement::Forward forward(fSpeed);
// handles stopped
Movement::Stop stopped;

void setup() {
  // setup serial link
  Serial.begin(9600);
  
  pinMode(NINA_RESETN, OUTPUT);         
  digitalWrite(NINA_RESETN, LOW);
  SerialNina.begin(115200);

  //SerialNina.write(SerialNina.read());
  Serial.println("hello world");

  //init led structs
  pinMode(oLedPin, OUTPUT);
  oLed = {.pin = oLedPin};
  pinMode(rLedPin, OUTPUT);
  rLed = {.pin = rLedPin};
  pinMode(gLedPin, OUTPUT);
  gLed = {.pin = gLedPin};

  // init the sensors
  //pinMode(os1Pin, INPUT);
  os1.pin = os1Pin;
  //pinMode(os2Pin, INPUT);
  os2.pin = os2Pin;
  //pinMode(os3Pin, INPUT);
  os3.pin = os3Pin;
  //pinMode(os4Pin, INPUT);
  os4.pin = os4Pin;
  //pinMode(bLDRPin, INPUT);
  bLDR.pin = bLDRPin;
  //pinMode(rLDRPin, INPUT);
  rLDR.pin = rLDRPin;

  // initialise the motors
  AFMS.begin();
  
}


// general function to apply a motorSetting struct onto the motors
void setMotors(Movement::MotorSetting mSetting) {
  //l.logln("motors set");
  m1->setSpeed(mSetting.speeds[0]);
  m2->setSpeed(mSetting.speeds[1]);
  //l.logln(mSetting.speeds[0]);
  
  // handle if the motors are reversed so forward -> backward
  if (!MOTORSREVERSED) {
    m1->run(mSetting.directions[0]);
    m2->run(mSetting.directions[1]);
  } else {
    if (mSetting.directions[0] == FORWARD) {
      m1->run(BACKWARD);
    } else {
      m1->run(FORWARD);
    }
    if (mSetting.directions[1] == FORWARD) {
      m2->run(BACKWARD);
    } else {
      m2->run(FORWARD);
    }
  }
  
  if ((mSetting.speeds[0] > 0 || mSetting.speeds[1] > 0 ) && !motorsActive) {
    motorsActive = true;
  } else if (mSetting.speeds[0] == 0 && mSetting.speeds[1] == 0 && motorsActive) {
    motorsActive = false;
    digitalWrite(oLed.pin, false);
  }
}

// 0 is black 1 is white
int lightToBit(int light) {
  if (light < 150){
    return 1;
  } else {
    return 0;
  }
}

//                           4  2  1
// returns an int with bits (s3 s2 s1)
// line on left => 100 => 4
int getLineVal(Sensor s1, Sensor s2, Sensor s3) {
  int lineVal = 0;
  lineVal |= lightToBit(analogRead(s1.pin));
  lineVal |= lightToBit(analogRead(s2.pin)) << 1;
  lineVal |= lightToBit(analogRead(s3.pin)) << 2;
  return lineVal;
}

// gets the colour of what is in front of the colour sensor
// won't be accurate unless depth sensor (os4) reads < 300
Color getColorVal(Led wLed, Sensor ) {
  digitalWrite(wLed.pin, true);
  delay(100); // delay to allow values to stabilise, needs testing
  int bVal = analogRead(bLDR.pin);
  int rVal = analogRead(rLDR.pin);
  delay(100); // test whether this is needed
  digitalWrite(wLed.pin, false);
  if (bVal < rVal){
    return BLUE;
  } else {
    return RED;  
  }
}

String getValsString(Sensor s1, Sensor s2, Sensor s3) {
  return String(lightToBit(analogRead(s3.pin))) + String(lightToBit(analogRead(s2.pin))) + String(lightToBit(analogRead(s1.pin)));
}

bool robotStopped = true;

// serial command reciever
String getSerialCommand() {
  String command = Serial.readString();
  command = command.substring(0, command.length()-1);
  return command;
} 

String getBTSerialCommand() {
  String command = SerialNina.readString();
  command = command.substring(0, command.length()/2);
  return command;
}



void commandHandler(String command) {
  l.logln(command);
  if (command == "stop" || command == "st") {
    robotStopped = true;
  } else if (command == "go" || command == "g") {
    robotStopped = false;
  }
}


unsigned long previousMillis = 0;
unsigned long oLedInterval = 500;

void loop() {
  //put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();

  //servo.write(min(180, int(currentMillis/100)));
  
  if (robotStopped) {
    setMotors(stopped.getMotorSetting());
  } else {
    int lineVal = getLineVal(os1, os2, os3);
    l.logln(getValsString(os1, os2, os3));
    setMotors(forward.getMotorSetting());
    //setMotors(lineFollower.getMotorSetting(lineVal));
    
  }
  
  if (Serial.available() > 0) {commandHandler(getSerialCommand());}
  if (SerialNina.available() > 0) {commandHandler(getBTSerialCommand());}


  // led flashes if the motors are active
  if (motorsActive) {
    if (currentMillis - previousMillis >= oLedInterval) {
      // save the last time you blinked the LED
      previousMillis = currentMillis;
  
      // set the LED with the ledState of the variable:
      digitalWrite(oLed.pin, !oLed.state);
      oLed.state = !oLed.state;
    }
  }
}
