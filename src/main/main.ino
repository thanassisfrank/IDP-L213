// the main program file for the arduino
#include <Servo.h>
#include <Adafruit_MotorShield.h>
#include <WiFiNINA.h>
#include <Arduino_LSM6DS3.h>
#include "utils.h"
#include "movement.h"


// pin for each component =======================================================================
// avoid pin 2
const int oLedPin =             4; // Protoboard configuration, do not change unnecessarily
const int rLedPin =             3; // Protoboard configuration, do not change unnecessarily
const int gLedPin =             5; // Protoboard configuration, do not change unnecessarily
const int armServoPin =         9; // check again
const int clawServoPin =       10; // check again
const int distSensorPin =       7; // Protoboard configuration, do not change unnecessarily
// analogue pins ================================================================================
// avoid 4 and 5
// line sensor
const int rightSensorPin =       A0; // Protoboard configuration, do not change unnecessarily
const int leftSensorPin =        A1; // Protoboard configuration, do not change unnecessarily
// colour sensor
const int bLDRPin =              A3; // Protoboard configuration, do not change unnecessarily
const int rLDRPin =              A2; // Protoboard configuration, do not change unnecessarily
// robot speeds ================================================================================
const int fSpeed =              210; // motor speed for general movement (see movement.cpp) changes based on the weight
const int sSpeed =               70; // slow motor speed
const int minTSpeed =           100; // minimum turning speed
const float velConversion = 0.02052; // m/s at 70 fspeed
// parameters for turning =======================================================================
const float kp =              1.8; // kp for turning
// servo positions ==============================================================================
const int armServoUp =         40; // arm up
const int armServoDown =      150; // arm down
const int clawServoClosed =   165; // claw closed
const int clawServoOpen =     140; // claw  open
// ==============================================================================================


// timing variables and constants
unsigned long prevOLedChangeMillis = 0;
unsigned long prevMillis = 0;
unsigned long currMillis = 0;
unsigned long changeMillis = 0;
const unsigned long oLedInterval = 500; // for 2Hz flashing of oLed
unsigned long startTime;

// for use by the angle integration TEMP
unsigned long nowMillis = 0;
unsigned long lastMillis = 0;


// set with reference from overhead camera for precision turning and forward movement
float angleError = 0;
float distanceError = 0;

// last line values for crossing detection
int lastLineVals[3] = {0, 0, 0};

// program state variables
programStageName currentStage;
bool motorsActive = false; // global flag to keep track of if the motors are running to know when to flash oLed
int programIteration = 0;
bool stageShouldAdvance = false;
bool programHalted = true; // to indicate if the program is running
bool turnNotStarted = true;
bool moveStarted = false;
bool errorReceived = false;
int currentCrossingCount = 0;
unsigned long crossings_seen_time;
unsigned long moveStartedTime;
bool all_crossings_seen = false;
bool grabStarted = false;
int clawPos;

// object for logging 
Logger l((unsigned long)0, BOTH);

// Connects to motor shield to command wheel movement
Adafruit_MotorShield AFMS = Adafruit_MotorShield();
Adafruit_DCMotor *leftMotor = AFMS.getMotor(4);
Adafruit_DCMotor *rightMotor = AFMS.getMotor(3);

// connects to servos to command servo movement
Servo armServo;
Servo clawServo;

bool MOTORSREVERSED = true; // in case motor direction flipped, then just invert this flag

// create led structs
Led oLed;
Led rLed;
Led gLed;

//create sensor structs
//optoswitches
Sensor rightSensor;
Sensor leftSensor;
Sensor distSensor;
//LDRs
Sensor rLDR;
Sensor bLDR;

void setup() {
  // setup serial link and bluetooth
  Serial.begin(115200);
  Serial.println("Serial up");

  // initialise the motors
  AFMS.begin();
  Serial.println("Motor shield up");

  pinMode(NINA_RESETN, OUTPUT);
  digitalWrite(NINA_RESETN, LOW);
  SerialNina.begin(115200);

  // initialise IMU for gyroscope
  IMU.begin();
  Serial.println("IMU up");

  // Startup phrase
  Serial.println("hello world");

  //initialise led structs
  pinMode(oLedPin, OUTPUT);
  oLed = {.pin = oLedPin};
  oLed.interval = oLedInterval;
  pinMode(rLedPin, OUTPUT);
  rLed = {.pin = rLedPin};
  pinMode(gLedPin, OUTPUT);
  gLed = {.pin = gLedPin};
  Serial.println("leds setup");

  // initialise the sensors
  pinMode(rightSensorPin, INPUT);
  rightSensor.pin = rightSensorPin;
  pinMode(leftSensorPin, INPUT);
  leftSensor.pin = leftSensorPin;
  pinMode(distSensorPin, INPUT);
  distSensor.pin = distSensorPin;
  pinMode(bLDRPin, INPUT);
  bLDR.pin = bLDRPin;
  pinMode(rLDRPin, INPUT);
  rLDR.pin = rLDRPin;
  Serial.println("sensors setup");

  armServo.attach(armServoPin);
  clawServo.attach(clawServoPin);

  //angleError = 180; // this will be the value that we need to actually shift the robot by. Note that this can be a signed angle to indicate direction!
  

  l.logln("<Arduino is ready>");

//  programIteration = 1;
//  currentStage = LONG_TRAVERSE_1; // testing from the beginning
//  advanceStage();
}

// general function to apply a motorSetting struct onto the motors
// currently rhe motors are reversed
void setMotors(Movement::MotorSetting mSetting) {
  leftMotor->setSpeed(min((int)(1.02*(float)mSetting.speeds[0]), 255)); // 1.02 for trimming
  rightMotor->setSpeed(min(mSetting.speeds[1], 255));
  
  // here to correct the direction of movement in case the motors are connected backwards
  if (!MOTORSREVERSED) { // ie supposed to be going forward
    leftMotor->run(mSetting.directions[0]);
    rightMotor->run(mSetting.directions[1]);
  }
  else { // ie supposed to be going backward
    if (mSetting.directions[0] == FORWARD) {
      leftMotor->run(BACKWARD);
    } else {
      leftMotor->run(FORWARD);
    }
    if (mSetting.directions[1] == FORWARD) {
      rightMotor->run(BACKWARD);
    } else {
      rightMotor->run(FORWARD);
    }
  }

  if ((mSetting.speeds[0] > 0 || mSetting.speeds[1] > 0 ) && !motorsActive) { // We have asked it to start moving but the motorsActive flag is still true
    motorsActive = true;
  } else if (mSetting.speeds[0] == 0 && mSetting.speeds[1] == 0 && motorsActive) { // We have asked it to stop moving motorsActive flag is still false
    motorsActive = false;
    digitalWrite(oLed.pin, false);
  }
}

float gx, gy, gz;
float angleDelta;
Movement::MotorSetting mSetting;

// must have last 3 values as a crossing for it to confirm that it was in fact a crossing, 
// and add one to the crossing counter
bool checkForCrossing(int lineVal) { 
  if (
    lastLineVals[0] != 0b11 && 
    lastLineVals[1] != 0b11 &&
    lastLineVals[2] != 0b11 && 
    lineVal == 0b11
  ) {
    lastLineVals[2] = lastLineVals[1];
    lastLineVals[1] = lastLineVals[0];
    lastLineVals[0] = lineVal;
    return true;
  } else {
    lastLineVals[2] = lastLineVals[1];
    lastLineVals[1] = lastLineVals[0];
    lastLineVals[0] = lineVal;
    return false;
  }
}

Movement::MotorSetting minimiseAngleError() {
  Movement::MotorSetting mSetting;
  if (IMU.gyroscopeAvailable()) {
    IMU.readGyroscope(gx, gy, gz);
  }
  //l.logln(angleError);
  // the 0.45 offset is to account for the fact that the suspended vehicle (not rotating) seems to read a -ve value
  angleDelta = ((gz + 0.45) / 1000) * (float)changeMillis ; 
  // set motors to kp*angle error
  if (angleError < 0) {
    mSetting =  Movement::getMovement(Movement::SPIN, min((int)(kp * angleError), -minTSpeed), 0);
  } else {
    mSetting =  Movement::getMovement(Movement::SPIN, max((int)(kp * angleError), minTSpeed), 0);
  }
  angleError -= angleDelta / (0.8*1.12);
  return mSetting;
}

Movement::MotorSetting minimiseDistanceError() {
  Movement::MotorSetting mSetting;
  l.logln("moving forward");
  if (distanceError < 0) {
    mSetting = Movement::getMovement(Movement::STRAIGHT, -sSpeed, 0);
    distanceError += (float)changeMillis/1000.0 * velConversion;
  } else {
    mSetting = Movement::getMovement(Movement::STRAIGHT, sSpeed, 0);
    distanceError -= (float)changeMillis/1000.0 * velConversion;
  }
  return mSetting;
  
}


// performs any functions associated with the given stage and returns a motor setting
Movement::MotorSetting performStage(programStageName stageName, int lineVal) {
  //  Serial.print("stage: ");
  //  Serial.println(stageName);
  // leave the case clauses in this order  
  // (10 March update: changing to the actual order of stages for ease of reading, hopefully this does not change anything)
 
  switch (stageName) {

    case GRAB_BLOCK: // had to change order because it works better, somehow =======================================================================================================================
      l.logln("block grabbed");
      stageShouldAdvance = true; // here for now
      // if (!grabStarted) {
      //   clawServo.write(clawServoOpen);
      //   clawPos = clawServoOpen;
      //   grabStarted = true;
      // } else if (clawPos >= clawServoClosed){
      //   stageShouldAdvance = true;
      //   return Movement::getMovement(Movement::STOP, 0, 0);
      // } else {
      //   clawPos += 1;
      //   clawServo.write(clawPos);
      // }
      return Movement::getMovement(Movement::STRAIGHT, 90, 0);
      break;

      case RAISE_BLOCK: // ==========================================================================================================================================================
        l.logln("block raised");
        stageShouldAdvance = true;
        // raise arm servo in a loop
        for (int i = armServoDown; i < armServoUp; i++) {
          armServo.write(i);
        }
        return Movement::getMovement(Movement::STOP, 0, 0);
        break;

    case MOVE_TO_LINE_FROM_BLOCK: // ==========================================================================================================================================================
        l.logln("moving back to the line");
        stageShouldAdvance = true;
        break;
        
    case LONG_TRAVERSE_1: // ==========================================================================================================================================================
      l.logln("line follow the second time!");
      // check is we've hit a crossing
      if (checkForCrossing(lineVal)) {
        currentCrossingCount++;
        l.logln("lf crossing");
      }
      if (currentCrossingCount == 2) {
        if (!all_crossings_seen) {
          crossings_seen_time = currMillis;
          all_crossings_seen = true;
        }
        if (currMillis - crossings_seen_time > 3100) {
          stageShouldAdvance = true;
        }
        return Movement::getMovement(Movement::LINE_FOLLOW, fSpeed, lineVal);
      } else {
        return Movement::getMovement(Movement::LINE_FOLLOW, fSpeed, lineVal);
      }
      break;


    case START: // ==========================================================================================================================================================
      // raise the grabber to the highest point
      armServo.write(armServoUp);
      if (checkForCrossing(lineVal)) {
        currentCrossingCount++;
        l.logln("start crossing");
      }
      
      if (currentCrossingCount == 2 || programIteration > 0) {
        stageShouldAdvance = true;
        return Movement::getMovement(Movement::STOP, 0, 0);
      } else {
        return Movement::getMovement(Movement::LINE_FOLLOW, fSpeed, lineVal);
      }
      break;

    case LONG_TRAVERSE_0: // ==========================================================================================================================================================
      //Serial.println("line follow");
      // check is we've hit a crossing
      if (checkForCrossing(lineVal)) {
        currentCrossingCount++;
        l.logln("lf crossing");
      }
      if (currentCrossingCount == 1) {
        if (!all_crossings_seen) {
          crossings_seen_time = currMillis;
          all_crossings_seen = true;
        }
        if (currMillis - crossings_seen_time > 2000) { //change back to 2000 =================
          stageShouldAdvance = true;
          return Movement::getMovement(Movement::STOP, 0, 0);
        }
        return Movement::getMovement(Movement::LINE_FOLLOW, fSpeed, lineVal);
      } else {
        return Movement::getMovement(Movement::LINE_FOLLOW, fSpeed, lineVal);
      }
      break;

    case MOVE_TO_DROP_ZONE:
    case TURN_TO_BLOCK: // ==========================================================================================================================================================
      armServo.write(armServoDown);
      if (abs(angleError) > 1) {
        // turn to correct angle
        //l.logln("turn");
        return minimiseAngleError();
      } else if (fabs(distanceError) > 0.01) {//(distanceError > 0.05 || distanceError < -0.05){
        l.logln(distanceError);
        l.logln("fwd");
        // move to correct distance
        return minimiseDistanceError();
      } else if (errorReceived){
        // error has been minimised 
        stageShouldAdvance = true;
      } else {
        // error has not been received yet
        //l.logln("waiting");
      }
      //l.logln("stopped");
      return Movement::getMovement(Movement::STOP, 0, 0); // stops when angle has been reached
      break;
      
    // case MOVE_TO_DROP_ZONE: // ==========================================================================================================================================================      
    //   Serial.println("turning");

    //   if (abs(angleError) > 1) {
    //     return goToLocation();
    //   } else {
    //     if (!moveStarted) {
    //       moveStarted = true;
    //       moveStartedTime = currMillis;
    //     }
    //     if (currMillis - moveStartedTime > 0.2/velConversion) {
    //       stageShouldAdvance = true;
    //       return Movement::getMovement(Movement::STOP, 0, 0);
    //     } else {
    //       return Movement::getMovement(Movement::STRAIGHT, -70, 0);
    //     }
    //     stageShouldAdvance = true;
    //     return Movement::getMovement(Movement::STOP, 0, 0); // stops when angle has been reached
    //   }
    //   break;

      case MOVE_TO_BLOCK: // ==========================================================================================================================================================
      l.logln("move to block");
        if (digitalRead(distSensor.pin) == 0) {
          stageShouldAdvance = true;
          return Movement::getMovement(Movement::STOP, 0, 0);
        // } else if (programIteration == 0){
        //   // if block is too far away, keep moving towards it
        //   return Movement::getMovement(Movement::LINE_FOLLOW, -70, lineVal);
        } else { // openCV ========================================================================================================================================
         return Movement::getMovement(Movement::STRAIGHT, -70, 0);
        }
        break;

     case LOWER_BLOCK: // ==========================================================================================================================================================
      l.logln("lowered block");
      // lower arm servo in a loop
      // return Movement::getMovement(Movement::STOP, 0, 0);
      stageShouldAdvance = true;
      break;
     
     case SENSE_BLOCK_COLOR: // ==========================================================================================================================================================
      Color blockCol = getColorVal(rLDR, bLDR);
      if (blockCol == BLUE) {
        //l.logln("blue");
        digitalWrite(gLed.pin, true);
        delay(5100);
        digitalWrite(gLed.pin, false);
      }
      else {
        //l.logln("red");
        digitalWrite(rLed.pin, true);
        delay(5100);
        digitalWrite(rLed.pin, false);
      }
      l.logln("block colour got");
      stageShouldAdvance = true;
      break;
   
    case DROP_BLOCK: // ==========================================================================================================================================================
      //l.logln("stopped");
      return Movement::getMovement(Movement::STOP, 0, 0);
      break;
    
    case MOVE_TO_LINE_FROM_DROP: // ==========================================================================================================================================================
      break;
  }
  return Movement::getMovement(Movement::STOP, 0, 0);
}

// handles advancing stages inc. any setup
void advanceStage() {
  currentCrossingCount = 0;
  turnNotStarted = true;
  errorReceived = false;
  grabStarted = false;
  moveStarted = false;
  all_crossings_seen = false;
  stageShouldAdvance = false;
  if (currentStage == MOVE_TO_LINE_FROM_DROP) {
    programIteration++;
  }
  currentStage = static_cast<programStageName>(currentStage + 1);

  // do initial setup for the stages that need it
  switch (currentStage) {
    case TURN_TO_BLOCK:
      if (programIteration == 0) {
        // on first time, angle is 180
        angleError = -180;
        distanceError = 0;
        errorReceived = true;
      } else {
        // send a request 
        l.logln("<block>");
      }
      break;
    case MOVE_TO_DROP_ZONE:
      l.logln("<1>"); // this should change
      break; 
    case MOVE_TO_LINE_FROM_BLOCK:
      angleError = 180;
      distanceError = 0;
      errorReceived = true;
      break;
      
  }
  l.logln("stage advanced");
}

// grabs string from serial
String getSerialCommand() {
  String command = Serial.readString();
  command = command.substring(0, command.length());
  return command;
}

// grabs string from the bluetooth serial
String getBTSerialCommand() {
  String command = SerialNina.readString();
  return command;
}

// What does every input command to the Arduino do (raises flags that have corresponding actions)
void commandHandler(String command) {
  l.logln("command received");

  if (command == "stop" || command == "stopstop") {
    //l.logln("stopping");
    programHalted = true;
    l.log("time: ");
    l.logln((int)(currMillis-startTime));
  } else if (command == "go" || command == "gogo" || command == "<go>") {
    //l.logln("starting");
    programHalted = false;
    startTime = currMillis;
  } else if (command.startsWith("<")) {
    // received error
    int sepInd = command.indexOf(",");
    angleError = command.substring(1, sepInd).toFloat();
    distanceError = command.substring(sepInd + 1, command.length() - 1).toFloat();
    l.logln(angleError);
    l.logln(distanceError);
    errorReceived = true;
  }
}

void loop() {
  //put your main code here, to run repeatedly: ======================================================================================================================================
  //Serial.println("loop"); // to check if the loop is running
  currMillis = millis();
  changeMillis = currMillis - prevMillis;
  //Serial.println(getValsString(leftSensor, rightSensor)); // print out line sensor values

  // IMU.readGyroscope(gx, gy, gz);
  // l.logln(float(gz+0.95));
  

  if (programHalted) {
    setMotors(Movement::getMovement(Movement::STOP, 0, 0));
  } else {
    if (stageShouldAdvance) {
      advanceStage();
    } else {
      int lineVal = getLineVal(leftSensor, rightSensor);
      setMotors(performStage(currentStage, lineVal));
    }    
  }

  // if (motorsActive) {
  //   flashLed(currMillis - prevOLedChangeMillis, oLed);
  // }

   //  oLed flashes if the motors are active
   if (motorsActive) {
     if (currMillis - prevOLedChangeMillis >= oLedInterval) {
       // save the last time you blinked the LED
       prevOLedChangeMillis = currMillis;
       // set the LED with the ledState of the variable:
       digitalWrite(oLed.pin, !oLed.state);
       oLed.state = !oLed.state;
     }
   }

  // scan for any commands in the BT or USB serial buffers
  if (Serial.available() > 0) {commandHandler(getSerialCommand());}
  if (SerialNina.available() > 0) {commandHandler(getBTSerialCommand());}
  prevMillis = currMillis;
}
