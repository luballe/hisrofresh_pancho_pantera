#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h> // include i/o class header
#include <EEPROM.h>

hd44780_I2Cexp lcd; // declare lcd object: auto locate & config display for hd44780 chip

/*
Liquid flow rate sensor -DIYhacking.com Arvind Sanjeev

Measure the liquid/water flow rate using this code. 
Connect Vcc and Gnd of sensor to arduino, and the 
signal line to arduino digital pin 2.
 
 */

#define FLOWS_SIZE 3

byte statusLed    = 13;

byte sensorInterrupt = 0;  // 0 = digital pin 2
byte sensorPin       = 2;

int RelayPin_hidrofresh = 3; 
int RelayPin_calentador = 4; 

// The hall-effect flow sensor outputs approximately 4.5 pulses per second per
// litre/minute of flow.
float calibrationFactor = 6.8;
float caudal_minimo = 0.4;

volatile byte pulseCount;  

float flowRate;
float flowMilliLitres;
unsigned long totalMilliLitres;
float currentLitres;
float accumLitres;

unsigned long oldTime;
float flowsArray[FLOWS_SIZE];
int flowsIndex;

float flowAverage;
int activatedFlag;
int eeAddress; //EEPROM address to start reading from and writing to

void setup()
{
  // Initialize output pins. We set up as INPUT_PULLUP to avoid unwanted glitch activation on start-up
  pinMode(RelayPin_hidrofresh, INPUT_PULLUP);
  pinMode(RelayPin_calentador, INPUT_PULLUP);

  // Initialize a serial connection for reporting values to the host
  Serial.begin(9600);
   
  // Set up the status LED line as an output
  pinMode(statusLed, OUTPUT);
  
  
  pinMode(sensorPin, INPUT);
  digitalWrite(sensorPin, HIGH);

  eeAddress = 0;

  pulseCount        = 0;
  flowRate          = 0.0;
  flowMilliLitres   = 0;
  totalMilliLitres  = 0;
  oldTime           = 0;
  currentLitres     = 0.0;
  //accumLitres       = 0.0;
  EEPROM.get(eeAddress, accumLitres);

  for(int i=0;i<FLOWS_SIZE;i++)
  {
    flowsArray[i]=0.0;
    
  }
  flowAverage=0.0;

  // The Hall-effect sensor is connected to pin 2 which uses interrupt 0.
  // Configured to trigger on a FALLING state change (transition from HIGH
  // state to LOW state)
  attachInterrupt(sensorInterrupt, pulseCounter, FALLING);

  // initialize LCD with number of columns and rows:
  lcd.begin(20, 4);

  flowsIndex=0;
  activatedFlag = 0;
  // Initialize relays 
  pinMode(RelayPin_hidrofresh, OUTPUT);
  pinMode(RelayPin_calentador, OUTPUT);

  //digitalWrite(RelayPin_hidrofresh, HIGH);
  //digitalWrite(RelayPin_calentador, HIGH);

}

/**
 * Main program loop
 */
void loop()
{
   
   if((millis() - oldTime) > 1000)    // Only process counters once per second
   { 
    // Disable the interrupt while calculating flow rate and sending the value to
    // the host
    detachInterrupt(sensorInterrupt);
        
    // Because this loop may not complete in exactly 1 second intervals we calculate
    // the number of milliseconds that have passed since the last execution and use
    // that to scale the output. We also apply the calibrationFactor to scale the output
    // based on the number of pulses per second per units of measure (litres/minute in
    // this case) coming from the sensor.
    flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
    
    // Note the time this processing pass was executed. Note that because we've
    // disabled interrupts the millis() function won't actually be incrementing right
    // at this point, but it will still return the value it was set to just before
    // interrupts went away.
    oldTime = millis();
    
    // Divide the flow rate in litres/minute by 60 to determine how many litres have
    // passed through the sensor in this 1 second interval, then multiply by 1000 to
    // convert to millilitres.
    flowMilliLitres = (flowRate / 60) * 1000;
    
    // Add the millilitres passed in this second to the cumulative total
    totalMilliLitres += flowMilliLitres;
      
    unsigned int frac;
    flowsArray[flowsIndex]=flowRate;
    flowAverage=arrAvg();
    currentLitres = currentLitres + float(flowMilliLitres)/1000;
    accumLitres   = accumLitres + float(flowMilliLitres)/1000;
    
    // Print the flow rate for this second in litres / minute
    Serial.print("Flow rate: ");
    Serial.print(flowAverage);  // Print the integer part of the variable
    Serial.print("L/min");
    // Print the current litres flowed
    Serial.print("\t");       // Print tab space
    Serial.print("Current: ");        
    Serial.print(currentLitres);
    Serial.print("L"); 
    // Print the cumulative total of litres flowed since starting
    Serial.print("\t");       // Print tab space
    Serial.print("Accum: ");        
    Serial.print(accumLitres);
    Serial.print("L"); 

    String buf;
    buf += F("Flujo: ");
    buf += String(flowAverage, 2);
    buf += F(" Lts/min");
    lcd.setCursor(0, 0);
    lcd.print(buf);

    buf = "";
    buf += F("Actual: ");
    buf += String(currentLitres, 2);
    buf += F(" Lts");
    lcd.setCursor(0, 1);
    lcd.print(buf);

    buf = "";
    buf += F("Acum: ");
    buf += String(accumLitres, 2);
    buf += F(" Lts");
    lcd.setCursor(0, 2);
    lcd.print(buf);

    // Reset the pulse counter so we can start incrementing again
    pulseCount = 0;
    
    // Enable the interrupt again now that we've finished sending output
    attachInterrupt(sensorInterrupt, pulseCounter, FALLING);

    if (flowAverage > caudal_minimo){
      digitalWrite(RelayPin_hidrofresh, LOW);
      digitalWrite(RelayPin_calentador, LOW);
      Serial.println(" Hidro ON");
      if (activatedFlag == 0)
      {
        currentLitres = 0.0;
        activatedFlag = 1;
      }
    }
    else
    {
      digitalWrite(RelayPin_hidrofresh, HIGH);
      digitalWrite(RelayPin_calentador, HIGH);
      Serial.println(" Hidro OFF");
      if (activatedFlag == 1)
      {
        EEPROM.put(eeAddress, accumLitres);
        activatedFlag = 0;
      }
    }
    flowsIndex=flowsIndex+1;
    if(flowsIndex==FLOWS_SIZE)
    {
      flowsIndex=0;
    }
  }
}

/*
Insterrupt Service Routine
 */
void pulseCounter()
{
  // Increment the pulse counter
  pulseCount++;
}

float arrAvg()
{
  float accum=0.0;
  float avg=0.0;
  for(int i=0;i<FLOWS_SIZE;i++)
  {
    accum=accum+flowsArray[i];
  }
  avg=accum/FLOWS_SIZE;
  return avg;

}
