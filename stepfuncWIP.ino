//Stepping the telescope 1 deg takes x steps so round(x*deltaDeg) will get you the amount of steps you need to take to get from point a to point b
//where deltaDeg = (targetBearing-currentBearing)

float currentX = 3.141;
float targetX = 6.282;
float stepsX;
int counterX;

float currentY;
float targetY;
float stepsY;
int counterY;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

}

void loop() {
  getStepsX(currentX, targetX, stepsX, counterX);
  getStepsY(currentY, targetY, stepsY, counterY);
}

void getStepsX(float currentBearing, float targetBearing, int amountOfSteps, int stepCounter){
  int stepsPerDeg = 1;

  float deltaDeg = targetBearing-currentBearing;
  amountOfSteps = stepsPerDeg*deltaDeg;

  stepCounter += round(amountOfSteps);
  Serial.print("G01 x");
  Serial.println(round(stepCounter));
}

void getStepsY(float currentBearing, float targetBearing, int amountOfSteps, int stepCounter){
  int stepsPerDeg = 1;


  float deltaDeg = targetBearing-currentBearing;
  amountOfSteps = (stepsPerDeg*deltaDeg);

  stepCounter += round(amountOfSteps);
  Serial1.print("G01 y");
  Serial1.println(round(stepCounter));
}