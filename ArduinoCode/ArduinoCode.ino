#include <FastLED.h>
#include <SoftwareSerial.h> 
#include <OneWire.h> 
#include <DallasTemperature.h>

int SER_Pin = 12;   //pin 14 on the 75HC595
int RCLK_Pin = 11;  //pin 12 on the 75HC595
int SRCLK_Pin = 10; //pin 11 on the 75HC595

int RX_GPS = 5; //RX pin on GPS module
SoftwareSerial SoftSerial(RX_GPS, 0);

#define LED_TYPE WS2812
#define NUM_LEDS 4
#define DATA_PIN 3
CRGB leds[NUM_LEDS];

OneWire oneWire(6);
DallasTemperature sensors(&oneWire);

#define shiftRegCount 4
#define RegisterPins shiftRegCount * 8
boolean registers[RegisterPins];

// Limitswitch Pin, Stepper motor pins shiftregister offset, Calibration offset, CurrentDigit, Amount of Movement left
int MOTORS[4][5] = {
  {A0, 0, 20, 0, 0},
  {A3, 8, 40, 0, 0},
  {2, 16, 30, 0, 0},
  {8, 24, 10, 0, 0}
};

//Switch 1 and 2 pins used for changing color and modes
int SW1 = A5;
int SW2 = A2;

int DayColor[] = {255, 255, 255};
int NightColor[] = {80, 255, 0};

//Clear entire shift register
void clearRegisters(){
  for(int i = RegisterPins - 1; i >=  0; i--){
     registers[i] = LOW;
  }
} 
//Update register
void writeRegisters(){
  digitalWrite(RCLK_Pin, LOW);
  for(int i = RegisterPins - 1; i >=  0; i--){
    digitalWrite(SRCLK_Pin, LOW);
    int val = registers[i];
    digitalWrite(SER_Pin, val);
    digitalWrite(SRCLK_Pin, HIGH);
  }
  digitalWrite(RCLK_Pin, HIGH);
}

//Motor step delay lower then 500 has issues
int motorSpeed = 1000;

char currentDate[] = "000000";
char currentTime[] = "111111.00";

int currentTemp = 0;
int TimeFix = 0;

void setup() {
  FastLED.addLeds<LED_TYPE, DATA_PIN>(leds, NUM_LEDS);
  sensors.begin(); 
  
  Serial.begin(9600);
  
  pinMode(SER_Pin, OUTPUT);
  pinMode(RCLK_Pin, OUTPUT);
  pinMode(SRCLK_Pin, OUTPUT);
  
  for(int x = 0; x < 4; x++){
    pinMode(MOTORS[x][0], INPUT);
  }

  pinMode(SW1, INPUT);
  pinMode(SW2, INPUT);

  //reset all register pins
  clearRegisters();
  writeRegisters();

  for(int x = 0; x < 4; x++){
    calibrate(MOTORS[x]);
  }
  clearRegisters();
  writeRegisters();
  SoftSerial.begin(9600);
}

int mode = 1;
int colorMode = 0;
long lastBlink = 0;
int blinkBright = 0;

void loop(){    
   if(digitalRead(SW1)){
    mode = !mode;
   }
   
   if(digitalRead(SW2)){
    colorMode = !colorMode;
   }
   //testMode();
   
   if(mode){
    updateDateTime();
    showTime();
   
    if(millis()-lastBlink > 1000){
      lastBlink = millis();
      blinkBright = !blinkBright;
      if(colorMode){
        Background(blinkBright, DayColor);
      }else{
        Background(blinkBright, NightColor);
      }
    }
   }else{
    updateTemp();
    showTemp();
    BackgroundTemp();
   }
     Serial.println(currentTime);
}

void testMode(){
  int i = 0;
  while (Serial.available() > 0)
  {
    currentTime[i] = Serial.read();
    i++;
  }
  Serial.println(currentTime);
}

//Update time when it changes
void showTime(){
  //Stop the serial connection with GPS when driving steppers
  //Because it causes lag
  if(MOTORS[0][4] != 0 || MOTORS[1][4] != 0 || MOTORS[2][4] != 0 || MOTORS[3][4] != 0){
    SoftSerial.end();
  }else{
     SoftSerial.begin(9600);
  }

  //Doing this is ugly but the fast way by substracting -48 from the ASCCI value sometimes caused strange behavior
  //Converting to string first seems to work best
  char tmp[2];
  tmp[1] = 0; 
  for(int x = 0; x < 4; x++){
    tmp[0] = currentTime[x];
    updateDigit2(MOTORS[x], atoi(tmp));
  }
}

void showTemp(){
  updateDigit(MOTORS[1], currentTemp / 1000);
  updateDigit(MOTORS[2], currentTemp / 100 % 10);
}

void updateTemp(){
  sensors.requestTemperatures();
  currentTemp = sensors.getTempCByIndex(0) * 100;
}

//Update digit pos type 1; is blocking not used currently
void updateDigit(int motor[], int digit){
  if(motor[3] != digit && digit >= 0 && digit <= 9){
    //SoftSerial.end();
    if(digit > motor[3]){
       int x = (digit-motor[3])* (280);
       while(x > 0){
       clockwise(motor[1], motorSpeed);
       x--;
      }
    } else{
      int x = (motor[3]-digit)*(280);
       while(x > 0){
       counterclockwise(motor[1], motorSpeed);
       x--;
      }
    }
    //SoftSerial.begin(9600);
    motor[3] = digit;
    clearRegisters();
    writeRegisters(); 
  }

}

//Update digit type 2; is non blocking
void updateDigit2(int motor[], int digit){
  if(motor[3] != digit && digit >= 0 && digit <= 9){
    if(digit > motor[3]){
       if(motor[4] == 0){
        motor[4] = (digit-motor[3])* (280);
       }
       clockwise(motor[1], motorSpeed);
       motor[4]--;  
      }
      else{
       if(motor[4] == 0){
        motor[4] = (motor[3]-digit)*(280);
       }
       counterclockwise(motor[1], motorSpeed);
       motor[4]--; 
      }
      
      if(motor[4] == 0){
       motor[3] = digit;
       clearRegisters();
       writeRegisters(); 
      }
    } 
  }

//Move motor back to zero
void calibrate(int motor[]){
  while(!digitalRead(motor[0])){
     counterclockwise(motor[1], 500);
  }
  //calibration offset
  for(int x = 0; x <= motor[2]; x++){
     clockwise(motor[1], 500);
  }
  motor[3] = 0;
  clearRegisters();
  writeRegisters();
}

//Very nice and cool compact code for reading GPS time
char tmpTime[] = "000000.00";
//GPS time header
char header[] = "$GPRMC";
int charCount = 0;
int match = 0;
int item = 0;
int itemCount = 0;
int timeReady = 0;
void updateDateTime(){
  while (SoftSerial.available() > 0) {
      char c = SoftSerial.read();
      Serial.write(c);
      if(match){
        if(item == 0 && c >= 48 && c <= 57){
           tmpTime[itemCount] = c;
           timeReady = 1;
           itemCount++;
        }
        if(item == 8 && c != ','){
           currentDate[itemCount] = c;
           itemCount++;
        }
        if(c == ','){
          item++;
          itemCount = 0;
        }
      }
      if (header[charCount] == c)
        {
          charCount++;
        }else{
          if(charCount == 6){
            match = 1;
          }
          charCount = 0;
        }
      if(c == '*'){
        match = 0;
        item = 0;
      }
}
//Add timezone in this add +2 hours
if(currentTime != tmpTime && timeReady){
  memcpy(currentTime, tmpTime, 9);
  int A = int(currentTime[0])-48;
  int B = int(currentTime[1])-48;
  
  int hour = A*10+(B+2);
  if(hour > 23){
    hour -= 24;
  }
  currentTime[0] = 48 + hour / 10;
  currentTime[1] = 48 + hour % 10;
}
}

void Background(int br, int color[]){
  leds[0] = CRGB(color[0], color[1], color[2]);
  leds[1] = CRGB(color[0], color[1], color[2]);
  leds[2] = CRGB(color[0], color[1], color[2]);
  leds[3] = CRGB(color[0], color[1], color[2]);
  if(br){
    FastLED.setBrightness(255);
  }else{
    FastLED.setBrightness(200);
  }
  
    FastLED.show();

}

void BackgroundTemp(){
  if(currentTemp < 15){
    leds[1] = CRGB(0, 0, 255);
    leds[2] = CRGB(0, 0, 255);
  }
  if(currentTemp > 20 && currentTemp < 25){
    leds[1] = CRGB(0, 255, 0);
    leds[2] = CRGB(0, 255, 0);
  }
  if(currentTemp > 25){
    leds[1] = CRGB(255, 0, 0);
    leds[2] = CRGB(255, 0, 0);
  }
  
  leds[0] = CRGB(0, 0, 0);
  leds[3] = CRGB(0, 0, 0);
  FastLED.show();
}

void setRegisterPin(int index, int value){
  registers[index] = value;
}

void counterclockwise (int offset, int motorSpeed){
  // 1
  setRegisterPin(1+offset, HIGH);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(4+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 2
  setRegisterPin(1+offset, HIGH);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(4+offset, LOW);
  writeRegisters(); 
  delayMicroseconds (motorSpeed);
  // 3
  setRegisterPin(1+offset, LOW);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(4+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 4
  setRegisterPin(1+offset, LOW);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(4+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 5
  setRegisterPin(1+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(4+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 6
  setRegisterPin(1+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(4+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds (motorSpeed);
  // 7
  setRegisterPin(1+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(4+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 8
  setRegisterPin(1+offset, HIGH);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(4+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
}

void clockwise(int offset, int motorSpeed){
  // 1
  setRegisterPin(4+offset, HIGH);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(1+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 2
  setRegisterPin(4+offset, HIGH);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(1+offset, LOW);
  writeRegisters(); 
  delayMicroseconds (motorSpeed);
  // 3
  setRegisterPin(4+offset, LOW);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(1+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 4
  setRegisterPin(4+offset, LOW);
  setRegisterPin(3+offset, HIGH);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(1+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 5
  setRegisterPin(4+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(1+offset, LOW);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 6
  setRegisterPin(4+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(2+offset, HIGH);
  setRegisterPin(1+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds (motorSpeed);
  // 7
  setRegisterPin(4+offset, LOW);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(1+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
  // 8
  setRegisterPin(4+offset, HIGH);
  setRegisterPin(3+offset, LOW);
  setRegisterPin(2+offset, LOW);
  setRegisterPin(1+offset, HIGH);
  writeRegisters(); 
  delayMicroseconds(motorSpeed);
}
