#include "heater.h"

// Temperature control parameters/variables
float Kp = 40;
float Kd = 800;
float Ki = 0.0;

// RT-PCR cycling parameters (degC, seconds)
int rt_temp = 55; // Reverse Transcription
int rt_time = 0;
int hs_temp = 100; // Hot Start
int hs_time = 10;
int an_temp = 55; // Annealing
int an_time = 2;
int de_temp = 100; // Denature
int de_time = 2;
int cycle_num = 40;
int MAX_PWM = 75;

int BLED_PWM = 0;
int RLED_PWM = 0;
int LED_STATE = 0;

boolean FAM_CHANNEL = true;
boolean CY5_CHANNEL = true;

int outputPWM = 0;

String temp;

void setupHeater() {
  pinMode(OUT1Pin, OUTPUT);
  pinMode(OUT2Pin, OUTPUT);
  pinMode(BLEDPin, OUTPUT);
  pinMode(RLEDPin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  
  analogWrite(OUT1Pin, 0);
  analogWrite(OUT2Pin, 0);
  digitalWrite(fanPin, LOW);
  analogWrite(BLEDPin, 255);
  analogWrite(RLEDPin, 255);
}

double readTemp() {
  double VR0_raw = analogRead(R0Pin);
  double VRx_raw = analogRead(RxPin);
  double VS_raw = 1023;
  double R0 = 14000;

  double Vout = VR0_raw - VRx_raw;

  double Rx = (R0 * (0.5 - Vout / VS_raw)) / (0.5 + Vout / VS_raw);
  double logRx = log(Rx);
  
  //100 kohm thermistor Steinhart-Hart coefficients
  double vA = 0.8269925494 * 0.001;
  double vB = 2.088185118 * 0.0001;
  double vC = 0.8054469376 * 0.0000001;
  double temp = 1 / (vA + (vB + (vC * logRx * logRx )) * logRx ) - 273.15;
  return temp;
}

float currentTemp;
float previousError =0;
float integralError =0;
float derivative =0;
float error =0;

// Set temperature (PID controlled)
void setTemp(float setPoint, float hold_time){
   setTemp(setPoint, hold_time,false);
}
void setTemp(float setPoint, float hold_time, boolean detect)
{
  previousError = 0;
  integralError = 0;
  derivative = 0;
  error = 0;

  // uses PID to set temperature
  boolean done = false;
  boolean setReached = false;
  boolean FAM_pic = detect && FAM_CHANNEL;
  boolean CY5_pic = detect && CY5_CHANNEL;
  boolean takingPic = false;
  float startTime;
  float printTime = millis();
  float stepduration = hold_time * 1000;

  //Serial.print("Setting temperature to " + String(setPoint) + " degC");
  while (!done) {
    // Calculate PID
    currentTemp = readTemp();
    error = setPoint - currentTemp;
    derivative = error - previousError;
    integralError = integralError + error;
    previousError = error;
    outputPWM = Kp * error + Ki * integralError + Kd * derivative;

  // PWM modifiers
    outputPWM = constrain(map((int)outputPWM, 0, 1000, 0, MAX_PWM), -255, MAX_PWM);

    // Fan cool if overshooting temperature
    if(error < -5){
      digitalWrite(fanPin,HIGH);
    }
    else if(error > 0){
      digitalWrite(fanPin, LOW);
    }

    // Bidirectional current control
    if (outputPWM < 0) {
      outputPWM = 0;
      analogWrite(OUT1Pin, 0);
      analogWrite(OUT2Pin, 0);
    }
    if (outputPWM >= 0) {
      analogWrite(OUT1Pin, outputPWM);
      analogWrite(OUT2Pin, 0);
    }

    // Timer starts counting down
    if (!setReached && abs(error) < 1)
    {
      // if temp hasn't been reached yet, check if it's within threshold
      startTime = millis();  // set timer
      setReached = true;        // target temp reached
      
      //Serial.print(F("\t Target temp reached."));
    }

    // (add time interval acquisition routine here)
    if (millis() - printTime >= time_int) {
      printTime = printTime + time_int;
      Serial.print("T,"); // indicates time/temp measurement for Pi logging
      Serial.print((printTime - init_time) / 1000.0);
      Serial.print(",");
      Serial.println(readTemp());
    }

    // End temperature hold when timer runs out and pictures have been taken
    if (setReached == true && (millis() - startTime) > stepduration) {
      //Serial.println(F("\t Hold temp complete."));
      if(takingPic){
        input(false);
        // Pi will send a "P" when the picture has been taken
        if(message == "P"){
          takingPic = false;
          //Turn off LEDs
          analogWrite(RLEDPin, 255);
          analogWrite(BLEDPin, 255);
          LED_STATE = 0;
          message = "";
        }
        
      }
      else if(FAM_pic){
        takingPic = true;
        analogWrite(RLEDPin, 255);
        analogWrite(BLEDPin, BLED_PWM);
        LED_STATE = 1;
        Serial.println("PB"); 
        FAM_pic = false;     
      }
      else if(CY5_pic){
        takingPic = true;
        analogWrite(BLEDPin, 255);
        analogWrite(RLEDPin, RLED_PWM);
        LED_STATE = 2;
        Serial.println("PR"); 
        CY5_pic = false;  
      }
      else{
        done = true;  // Temperature reached, held long enough time, pictures taken
        digitalWrite(fanPin,LOW);
        analogWrite(OUT1Pin, 0);
        analogWrite(OUT2Pin, 0);
      }      
    }

    // PID loop interval
    delay(interval);
  }
}

void cycle(){
  init_time = millis();
  // Reverse Transcription
  if(rt_time > 0.01){
    Serial.println("L,Reverse Transcription,START");
    setTemp(rt_temp, rt_time, false);
    Serial.println("L,Reverse Transcription,END");
  }
  // Hot Start
  if(hs_time > 0.01){
    Serial.println("L,Hot Start,START");
    setTemp(hs_temp, hs_time, false);
    Serial.println("L,Hot Start, END");
  }
  // Cycling
  // Print "C" at the beginning of each cycle followed by # of cycle - example: C,1 = cycle 1
  if(cycle_num > 0){
    Serial.println("L,Cycling, START");
    for(int i = 0; i<cycle_num; i++){
       Serial.print("C,");
       Serial.println(i+1);
       
       Serial.print("L,Denature,");
       Serial.println(i+1);
       setTemp(de_temp, de_time, false);
       //digitalWrite(fanPin,HIGH);

       Serial.print("L,Anneal,");
       Serial.println(i+1);
       setTemp(an_temp, an_time, FAM_CHANNEL || CY5_CHANNEL);
       //digitalWrite(fanPin,LOW);
    }  
    Serial.println("L,Cycling, END");
  }
  Serial.println("E"); 
}
