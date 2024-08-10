#include <floatToString.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <Keypad.h>
#include "Wire.h" // This library allows you to communicate with I2C devices.
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <MechaQMC5883.h>

MechaQMC5883 qmc;
LiquidCrystal_I2C lcd(0x27, 16, 4);



char altaz[16];
char date[16];
char time[16];
char planetName[16];
char telescopeAz[16];
char telescopeAlt[16];
String dIn;
String tIn;
String aIn;

float tAlt;
float tAz;
//accelerometer -> pitch/roll code from https://wiki.dfrobot.com/How_to_Use_a_Three-Axis_Accelerometer_for_Tilt_Sensing

const int MPU_ADDR = 0x68; // I2C address of the MPU-6050. If AD0 pin is set to HIGH, the I2C address will be 0x69.

int16_t accelerometer_x, accelerometer_y, accelerometer_z; // variables for accelerometer raw data
int16_t gyro_x, gyro_y, gyro_z; // variables for gyro raw data
int16_t temperature; // variables for temperature data
int x, y, z;                        //three axis acceleration data
double roll = 0.00, pitch = 0.00;       //Roll & Pitch are the angles which rotate by the axis X and 

void setup() {
  
  Serial.begin(9600);
  Serial.println("Starting");
  delay(500);

  Wire.begin();
  Wire.beginTransmission(MPU_ADDR); // Begins a transmission to the I2C slave (GY-521 board)
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0); // set to zero (wakes up the MPU-6050)
  Wire.endTransmission(true);

  qmc.init();
  qmc.setMode(Mode_Continuous,ODR_200Hz,RNG_2G,OSR_512);
  lcd.init();
  lcd.backlight();
  delay(250);
  lcd.print(":3");
}

void loop() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // starting with register 0x3B (ACCEL_XOUT_H) [MPU-6000 and MPU-6050 Register Map and Descriptions Revision 4.2, p.40]
  Wire.endTransmission(false); // the parameter indicates that the Arduino will send a restart. As a result, the connection is kept active.
  Wire.requestFrom(MPU_ADDR, 7*2, true); // request a total of 7*2=14 registers

  // "Wire.read()<<8 | Wire.read();" means two registers are read and stored in the same variable
  accelerometer_x = Wire.read()<<8 | Wire.read(); // reading registers: 0x3B (ACCEL_XOUT_H) and 0x3C (ACCEL_XOUT_L)
  accelerometer_y = Wire.read()<<8 | Wire.read(); // reading registers: 0x3D (ACCEL_YOUT_H) and 0x3E (ACCEL_YOUT_L)
  accelerometer_z = Wire.read()<<8 | Wire.read(); // reading registers: 0x3F (ACCEL_ZOUT_H) and 0x40 (ACCEL_ZOUT_L)
  
  RP_calculate();

  qmc.read(&x, &y, &z, &tAz);
  strcpy(telescopeAz, String(tAz).c_str());
  strcpy(telescopeAlt, String(pitch).c_str());

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(telescopeAlt);
  lcd.print("/");
  lcd.print(telescopeAz);
  delay(250);
}

void RP_calculate(){
  double x_Buff = accelerometer_x;
  double y_Buff = accelerometer_y;
  double z_Buff = accelerometer_z;
  //roll = atan2(y_Buff , z_Buff) * 57.3;
  pitch = -(atan2((- x_Buff) , sqrt(y_Buff * y_Buff + z_Buff * z_Buff)) * 57.3);
}
