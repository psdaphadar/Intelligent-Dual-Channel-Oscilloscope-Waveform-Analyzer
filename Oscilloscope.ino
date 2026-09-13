/*****************************************************************************************
Project : Intelligent Dual-Channel Oscilloscope & Waveform Analyzer
Author  : Partha Sarathi Daphadar
Date    : 13-09-2026
Copyright (c) 2026 Partha Sarathi Daphadar
All Rights Reserved.

LICENSE & TERMS OF USE:

PERMISSION:

This license applies to the software, source code, circuit diagrams, PCB design files,
documentation, and other project materials contained in this repository, unless otherwise
stated.

You may study, reference, and modify this project for personal, educational, or 
non-commercial research purposes only.

You must give proper credit to the author whenever you use or refer to this project or
any part of it.

RESTRICTIONS:

- Commercial use, selling, sublicensing, or redistribution of this project, in whole or
  in part, is strictly prohibited without prior written permission from the author.
- You may not remove or alter this copyright notice or these terms.

DISCLAIMER OF WARRANTY:

This project is provided "AS IS," without any warranties, express or implied, including
but not limited to fitness for a particular purpose, accuracy, or error-free operation.
The author makes no guarantee that it will work correctly or meet your specific needs.

LIMITATION OF LIABILITY:
You use this project at your own risk.
The author shall not be liable for any damages, losses, or problems arising from its use,
including hardware damage, data loss, or personal injury.

BY USING, COPYING, MODIFYING, OR DISTRIBUTING THIS PROJECT, YOU AGREE TO THESE TERMS.
*****************************************************************************************/


#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define TFT_CS    9
#define TFT_DC   10
#define TFT_RST  12
#define ADC_PIN1   A0
#define ADC_PIN2   A1
#define ADC_PIN3   A2

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
#define TFT_RGB 1 // 0 = BGR display, 1 = RGB display
#define TFT_TYPE 0 // 0 or 1 
#define GRID     tft.color565(80, 80, 80)
#define BUTTON_PIN 7

int prescaler = 128;

const char pName[23][7] PROGMEM = {"Freq:","Vmax:","Vmin:","Vpp :","Vavr:","Vrms:","RMS~:","Vdc :","RF:","CF:","FF:","Vamp:","Vabs:","Duty:","T :","PW:","Gain","Delay","COR","Vdrms","Vdpp","Fs:","Hn:n*"};
const uint8_t SAMPLES = 128;
uint8_t endPoint = SAMPLES;
uint8_t endPointCh1 = SAMPLES;
uint16_t sample1[SAMPLES];
uint16_t sample2[SAMPLES];
uint8_t y1Old[SAMPLES];
uint8_t y2Old[SAMPLES];
byte currentWindow = 0;
bool longPressDone = false;
float refVoltage = 0;
float vPP = 0;
float vAvg = 0;
float vRMS = 0;
float frequency = 0;
uint8_t peak1=0, peak2=0;
float cor = 0;
bool chIsSine = false;
bool ch1IsSine = false;
bool ch2IsSine = false;
bool ch1IsSineOld = false;
bool ch2IsSineOld = false;
bool sameFrequency = false;
float phaseDiff = 0;
float gainDB = 0;
float Delay = 0;
int8_t lagLeadOld = -99;
uint16_t samplingRateDispOld = -99;
unsigned long sampleInterval = 20;
float actualSampleInterval = 20.0;
uint8_t numEdges = 5;
const int thresholdHigh = 524;
const int thresholdLow  = 500;
long Fs = 2000; 
float riseTime = 0;
float fallTime = 0;
uint16_t harmonicArrayOld[12];
float oldValues[29];

void setup()
{
 analogReference(DEFAULT);
 pinMode(ADC_PIN1, INPUT);
 pinMode(ADC_PIN2, INPUT);
 pinMode(ADC_PIN3, INPUT);
 pinMode(BUTTON_PIN, INPUT_PULLUP);
 tft.cp437(true);
 ADCSRA = (ADCSRA & 0xF8) | 0x02;
 tft.initR(TFT_TYPE ? INITR_BLACKTAB : INITR_GREENTAB);
 digitalWrite(TFT_CS, LOW);
 digitalWrite(TFT_DC, LOW);
 SPI.transfer(0x36);
 digitalWrite(TFT_DC, HIGH);
 SPI.transfer(TFT_RGB ? 0xC0 : 0xC8);
 digitalWrite(TFT_CS, HIGH);
 tft.fillScreen(ST77XX_BLACK);
 tft.setTextColor(ST77XX_WHITE);
 tft.setTextSize(1);
 displayInfo();
 tft.drawRect(0,24,128,44,ST77XX_BLUE); 
 delay(3000);
 tft.fillScreen(ST77XX_BLACK);
 drawGrid();
 for(uint8_t i=0; i<29;i++)
  oldValues[i] = -99;
}

void displayInfo()
{
 tft.setCursor(15,30);
 tft.print(F("INTELLIGENT DUAL-"));
 tft.setCursor(4,42);
 tft.print(F("CHANNEL OSCILLOSCOPE"));
 tft.setCursor(10,54);
 tft.print(F("& WAVEFORM ANALYZER"));
 tft.setCursor(31,70);
 tft.print(F("Version 1.0"));
 tft.setCursor(2,104);
 tft.print(F("Developed by-"));
 tft.setCursor(40,112);
 tft.print(F("P. S. Daphadar"));
}

void drawGrid()
{
 for(int x=0;x<128;x+=16)
  tft.drawLine(x,2,x,66,GRID);
 tft.drawFastVLine(127,2,64,GRID);
 for(int y=0;y<=64;y+=16)
  tft.drawFastHLine(0,y+2,128,GRID);
 tft.drawFastHLine(0,32+2,128,ST77XX_RED);
}

void checkButton()
{
 bool state = digitalRead(BUTTON_PIN);
 if(state == LOW && !longPressDone)
 {
  currentWindow++;
  tft.fillScreen(ST77XX_BLACK);
  for(uint8_t i=0; i<29; i++)
   oldValues[i] = -99;
  lagLeadOld = -99;
  samplingRateDispOld = -99;
  for(uint8_t h=0; h<12; h++)
   harmonicArrayOld[h] = 0;
  if(currentWindow >= 5)
   currentWindow = 0;
  if(currentWindow <= 2)
   drawGrid();
  longPressDone = true;
 }
 if(state == HIGH)
  longPressDone = false;
}

float measureFrequency()
{
 int edgeCount = 0;
 bool lastState = false;
 unsigned long startTime = micros();
 unsigned long timeout = 1000000;
 while (edgeCount < numEdges)
 {
  if (micros() - startTime > timeout) return 0;
  int val;
  if (currentWindow == 0 || currentWindow == 2 || currentWindow == 3)
  {
   ADMUX = (ADMUX & 0xF0) | (ADC_PIN1 - A0);
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   val = ADC;
  }
  else
  {
   ADMUX = (ADMUX & 0xF0) | (ADC_PIN2 - A0);
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   val = ADC;
  }
  bool currentState = (val > thresholdHigh) ? true : (val < thresholdLow)  ? false : lastState;
  if (!lastState && currentState) edgeCount++;
  lastState = currentState;
 }
 unsigned long endTime = micros();
 float periodAvg = (endTime - startTime) / (float)numEdges;
 return 1000000.0 / periodAvg;
}

float correctedFreq(float fm)
{
 if (fm <= 0.0f) return fm;
 const float Ns = (float) numEdges;
 uint8_t adps = ADCSRA & 0x07;
 prescaler = 128;
 switch (adps)
 {
  case 0: prescaler = 2; break;
  case 1: prescaler = 2; break;
  case 2: prescaler = 4; break;
  case 3: prescaler = 8; break;
  case 4: prescaler = 16; break;
  case 5: prescaler = 32; break;
  case 6: prescaler = 64; break;
  case 7: prescaler = 128; break;
 }
 float t_conv = (13.0f * (float)prescaler) / (float)F_CPU;
 float s = t_conv * 0.5f;
 float Pm = 1.0f / fm;
 float denomPart = Pm - (s / Ns);
 if (denomPart <= 0.0f) return fm;
 float Pt = (Ns / (Ns - 1.0f)) * denomPart;
 if (Pt <= 0.0f) return fm;
 float f_corr = 1.0f / Pt;
 return f_corr;
}

void captureWave()
{
 float fr = measureFrequency();
 if(fr < 100) numEdges = 5; else numEdges = 20;
 frequency = correctedFreq(measureFrequency());
 if (frequency > 0.1)
 {
  float cyclesPerScreen = 2.0;
  Fs = frequency * (SAMPLES / cyclesPerScreen);
  sampleInterval = round(1000000.0 / Fs);
 }
 uint8_t adcPin;
 if (currentWindow == 0 || currentWindow == 2 || currentWindow == 3)
  adcPin = ADC_PIN1;
 else
  adcPin = ADC_PIN2;
 unsigned long timeout = micros();
 while (1)
 {
  while (analogRead(adcPin) > thresholdLow)
  {
   if (micros() - timeout > 200000)
    goto Capture;
  }
  while (analogRead(adcPin) < thresholdHigh)
  {
   if (micros() - timeout > 200000)
    goto Capture;
  }
  break;
 }
 Capture:
  if(currentWindow>2)
   setBestADCPrescaler(frequency);
  else
   ADCSRA = (ADCSRA & 0xF8) | 0x02;
  unsigned long tStart = micros();
  unsigned long nextSample = tStart;
  for(int i = 0; i < SAMPLES; i++)
  {
   while(micros() < nextSample);
   ADMUX = (ADMUX & 0xF0) | (ADC_PIN1 - A0);
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   sample1[i] = ADC;
   ADMUX = (ADMUX & 0xF0) | (ADC_PIN2 - A0);
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   ADCSRA |= (1 << ADSC);
   while(ADCSRA & (1 << ADSC));
   sample2[i] = ADC;
   nextSample += sampleInterval;
  }
  unsigned long tEnd = micros();
  actualSampleInterval = (float)(tEnd - tStart) / (SAMPLES - 1);
}

void eraseOldWaves(uint8_t *yOld)
{
 for (int x = 0; x < SAMPLES; x++)
 {
  if (x > 0)
  {
   if(abs(yOld[x]-yOld[x - 1])>10)
    tft.drawFastVLine(x - 1,min(yOld[x - 1] + 2, yOld[x] + 2),abs(yOld[x]-yOld[x - 1]),ST77XX_BLACK);
   else
    tft.drawLine(x - 1, yOld[x - 1] + 2, x, yOld[x] + 2, ST77XX_BLACK);
  }
  else
   tft.drawPixel(x, yOld[x] + 2, ST77XX_BLACK);
  }
}

void drawWave(uint16_t *sampleData, uint8_t *yOld, uint16_t color)
{
 int previousY = -1;
 for (int x = 0; x < SAMPLES; x++)
 {
  int y = map(sampleData[x], 0, 1023, 63, 0);
  if (previousY >= 0)
  {
   if(abs(y-previousY)>10)
    tft.drawFastVLine(x - 1,min(previousY + 2, y + 2),abs(y - previousY),color);
   else
    tft.drawLine(x - 1, previousY + 2, x, y + 2, color);
  }
  else
   tft.drawPixel(x, y + 2, color);
  yOld[x] = y;
  previousY = y;
 }
}

void waveAnalysis(uint16_t *arr)
{
 float vMin = 5.0;
 float vMax = -5.0;
 float voltage = 0;
 float rms = 0, avg = 0;
 int highCount = 0;
 float dutyCycle = 0;
 float pw = 0;
 endPoint = SAMPLES;
 int upperLevel = arr[0];
 int lowerLevel = upperLevel - 24;
 bool posHalf = false;
 int cycleCount = -1;
 for (int i = 0; i < SAMPLES; i++)
 {
  if(arr[i] >= upperLevel && !posHalf)
  {
   cycleCount ++;
   posHalf = true;
  }
  if(arr[i] <= lowerLevel && posHalf)
   posHalf = false;
  if(actualSampleInterval*(SAMPLES-1)*frequency/1000000.0 > 2 && cycleCount >= floor(actualSampleInterval*(SAMPLES-1)*frequency/1000000.0))
  {
   endPoint = i;
   break;
  }
 }
 for(int i=0; i<endPoint; i++)
 {
  voltage = arr[i] * 5.0/1023 - refVoltage; 
  if(voltage < vMin)
   vMin = voltage;
  if(voltage > vMax)
   vMax = voltage;
  avg += voltage;
  rms += voltage * voltage;
 }
 vPP = vMax - vMin;
 vAvg = avg/endPoint;
 vRMS = sqrt(rms/endPoint);
 float acRMS = sqrt(vRMS * vRMS - vAvg * vAvg);
 if (vPP > 0.2)
 {
  float sineRatio = acRMS / vPP;
  if (sineRatio > 0.32 && sineRatio < 0.38)
   chIsSine = true;
  else
   chIsSine = false;
 }
 else
  chIsSine = false;
 for(int i=0; i<endPoint; i++)
 {
  voltage = arr[i] * 5.0/1023; 
  if(voltage  > (vMin+vMax)/2.0 + refVoltage)
   highCount ++;
 }
 if(frequency > 0.1)
 {
  dutyCycle = ((float)highCount/endPoint) * 100.0;
  pw = highCount * (1.0/(frequency*endPoint)) * 1000.0;
 }
 if(currentWindow < 2)
 {
  updateValue(frequency, oldValues[14], 35, 83, 0, 7, 2,ST77XX_CYAN);
  updateValue(vMax, oldValues[0], 35, 92, 1, 5, 1,ST77XX_CYAN);
  updateValue(vMin, oldValues[1], 35, 101, 1, 5, 1,ST77XX_CYAN);
  updateValue(vPP, oldValues[2], 35, 110, 1, 5, 1,ST77XX_CYAN);
  updateValue(vAvg, oldValues[3], 35, 119, 1, 5, 1,ST77XX_CYAN);
  updateValue(vRMS, oldValues[4], 35, 128, 1, 5, 1,ST77XX_CYAN);
  updateValue(dutyCycle, oldValues[15], 100, 128, 0, 4, 4,ST77XX_CYAN);
  if(pw < 100)
   updateValue(pw, oldValues[16], 87, 146, 1, 6, 5,ST77XX_CYAN);
  else
   updateValue(pw, oldValues[16], 87, 146, 0, 6, 5,ST77XX_CYAN);
  float vRMSAC = sqrt(vRMS * vRMS - vAvg * vAvg);
  updateValue(vRMSAC, oldValues[5], 35, 137, 1, 5, 1,ST77XX_CYAN);
  float vDC = (vMax + vMin)/2.0;
  updateValue(vDC, oldValues[6], 35, 146, 1, 5, 1,ST77XX_CYAN);
  float vAbs = 0;
  for (int i = 0; i < endPoint; i++)
   vAbs += abs(arr[i]*5.0/1023 - refVoltage - vDC);
  vAbs /= endPoint;
  if(frequency < 0.1)
   vAbs = 0;
  float formFactor = 0;
  if(vAbs > 0)
   formFactor = vRMSAC/vAbs;
  if(vRMSAC < 0.1)
   formFactor = 999;
  updateValue(formFactor, oldValues[7], 100, 101, 2, 4, 0,ST77XX_CYAN);
  float vAmp = (vMax - vMin)/2.0;
  updateValue(vAmp, oldValues[8], 100, 110, 1, 4, 1,ST77XX_CYAN);
  updateValue(vAbs, oldValues[9], 100, 119, 1, 4, 1,ST77XX_CYAN);
  float timePeriod = 0;
  if(frequency > 0.1)
   timePeriod = 1000.0/frequency;
  else
   timePeriod = 99999;
  if(timePeriod < 100)
   updateValue(timePeriod, oldValues[10], 87, 137, 1, 6, 5,ST77XX_CYAN);
  else
   updateValue(timePeriod, oldValues[10], 87, 137, 0, 6, 5,ST77XX_CYAN);
  float crestFactor = 0;
  float rippleFactor = 0;
  if(vRMSAC > 0)
   crestFactor = max(abs(vMax-vAvg),abs(vMin-vAvg))/vRMSAC;
  if(abs(vAvg) > 0)
   rippleFactor = vRMSAC/abs(vAvg);
  else
   rippleFactor = 999;
  updateValue(rippleFactor, oldValues[19], 100, 83, 2, 4, 0,ST77XX_CYAN);
  updateValue(crestFactor, oldValues[18], 100, 92, 2, 4, 0,ST77XX_CYAN);
 }
}

void loop()
{
 unsigned long startProgram = millis();
 analogRead(ADC_PIN3);
 refVoltage = analogRead(ADC_PIN3) * 5.0/1023;
 checkButton();
 captureWave();
 if (currentWindow == 0)
 {
  eraseOldWaves(y1Old);
  drawGrid();
  drawWave(sample1, y1Old, ST77XX_GREEN);
 }
 if (currentWindow == 1)
 {
  eraseOldWaves(y2Old);
  drawGrid();
  drawWave(sample2, y2Old, ST77XX_YELLOW);
 }
 if (currentWindow == 2)
 {
  eraseOldWaves(y1Old);
  eraseOldWaves(y2Old);
  drawGrid();
  drawWave(sample1, y1Old, ST77XX_GREEN);
  drawWave(sample2, y2Old, ST77XX_YELLOW);
 }
 if (currentWindow == 0 || currentWindow == 2)
 {
  waveAnalysis(sample1);
  endPointCh1 = endPoint;
 }
 else if (currentWindow == 1)
  waveAnalysis(sample2);
 tft.setTextColor(ST77XX_WHITE);
 tft.setTextSize(1);
 if(currentWindow == 0 || currentWindow == 1)
 {   
  printHeading();
  float samplingRate = 1000000.0 / actualSampleInterval;
  uint16_t samplingRateDisp = 0;
  bool kiloUnit = false;
  if(round(samplingRate) < 10000)
   samplingRateDisp = round(samplingRate);
  else
  {
   samplingRateDisp = round(samplingRate)/1000.0;;
   kiloUnit = true;
  }
  if(frequency < 0.1)
  {
   samplingRateDisp = 0;
   kiloUnit = false;
  }
  if(abs(samplingRateDisp-samplingRateDispOld) > 0)
  {
   tft.fillRect(78,70,49,7, ST77XX_BLACK);
   tft.setCursor(78,70);
   tft.print(samplingRateDisp);
   if(kiloUnit)
    tft.print(F(" kS/s"));
   else
    tft.print(F(" S/s"));
  }
  samplingRateDispOld = samplingRateDisp;
  tft.setCursor(4,70);
  if(currentWindow == 0)
  {
   tft.setTextColor(ST77XX_GREEN);
   tft.print(F("CHANNEL-1"));
  }
  else
  {
   tft.setTextColor(ST77XX_YELLOW);
   tft.print(F("CHANNEL-2"));
  }
 }
 else if(currentWindow == 2)
 {
  tft.setTextColor(ST77XX_WHITE);
  for(uint8_t i=0; i<2;i++)
  {
   printDelta(0,90+i*9);
   displayText(i+4,10,90+i*9);
  }
  for(uint8_t i=0; i<5;i++)
  {
   displayText(i+16,0,108+i*9);
   tft.setCursor(34,108+i*9);
   tft.print(F(":"));
  }
  calculateComparison();
  phaseDifference(sample1, sample2);
  calculateDRMS(sample1, sample2);
  calculateVDPP(sample1, sample2);
  if(sameFrequency)
  {
   updateValue(gainDB, oldValues[21], 40, 108, 1, 7, 3,ST77XX_YELLOW);
   if(sign(Delay) != lagLeadOld)
    tft.fillRect(81,117,35,9,ST77XX_BLACK);
   if(abs(Delay) < 100)
    updateValue(Delay, oldValues[22], 40, 117, 2, 7, 5,ST77XX_YELLOW);
   else
    updateValue(Delay, oldValues[22], 40, 117, 1, 7, 5,ST77XX_YELLOW);
   tft.setTextColor(ST77XX_WHITE);
   tft.setCursor(81,117);
   if(abs(Delay) > 0)
   {
    if(Delay > 0)
     tft.print(F("(Lead)"));
    else
     tft.print(F("(Lag)"));
   }
   lagLeadOld = sign(Delay);
  }
  else
  {
   tft.fillRect(40,108,88,18,ST77XX_BLACK);
   oldValues[21] = -99;
   oldValues[22] = -99;
  }
  if(!ch1IsSine || !ch2IsSine)
  {
   if(ch1IsSine != ch1IsSineOld || ch2IsSine != ch2IsSineOld)
   {
    tft.fillRect(0,68,128,19,ST77XX_BLACK);
    oldValues[25] = -99;
    oldValues[26] = -99;
    oldValues[27] = -99;
    oldValues[28] = -99;
   }
  }
  if(!ch1IsSine || !ch2IsSine)
   displayRiseFallTime();
 }
 else
 {
  if(currentWindow == 3)
   calculateTHD(sample1);
  else
   calculateTHD(sample2);
 }
 ch1IsSineOld = ch1IsSine;
 ch2IsSineOld = ch2IsSine;
 while(millis() < startProgram + 1000);
}

void printHeading()
{
 tft.drawRect(0,80,128,75,ST77XX_BLUE);
 tft.setTextColor(ST77XX_WHITE);
 tft.setTextSize(1);
 displayText(21,61,70);
 for(uint8_t i=0; i<8;i++)
  displayText(i,4,83+i*9);
 for(uint8_t i=0; i<3;i++)
  displayText(i+8,82,83+i*9);
 for(uint8_t i=0; i<5;i++)
  displayText(i+11,70,110+i*9);
}

void drawArrow(int x, int y, int length, int direction, uint16_t color)
{
 int end = x + direction * length;
 int h1 = end - direction * 5;
 tft.drawLine(max(0, x), y, max(0, min(127, end)), y, color);
 if (end >= 0 && end <= 127)
 {
  tft.drawLine(end, y, max(0, min(127, h1)), y - 4, color);
  tft.drawLine(end, y, max(0, min(127, h1)), y + 4, color);
 }
}

float calculateFrequency(uint16_t *arr)
{
 int threshold = 0;
 int minADC = 1023;
 int maxADC = 0;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (arr[i] < minADC)
   minADC = arr[i];
  if (arr[i] > maxADC)
   maxADC = arr[i];
 }
 threshold = (minADC + maxADC) / 2;
 int h = 5;
 int upper = threshold + h;
 int lower = threshold - h;
 int crossings[10];
 int crossingCount = 0;
 bool above = false;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (!above && arr[i] >= upper)
  {
   if (crossingCount < 10)
   {
    crossings[crossingCount++] = i;
    above = true;
   }
  }
  else if (above && arr[i] <= lower)
   above = false;
 }
 if (crossingCount >= 2)
  goto cal;
 for (int i = 1; i < SAMPLES; i++)
 {
  if (arr[i-1] < arr[i] && arr[i] <= arr[i+1])
  {
   threshold = arr[i-1];
   break;
  }
 }
 upper = threshold + h;
 lower = threshold - h;
 for (int i = 0; i < 10; i++)
  crossings[i];
 crossingCount = 0;
 above = false;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (!above && arr[i] >= upper)
  {
   if (crossingCount < 10)
   {
    crossings[crossingCount++] = i;
    above = true;
   }
  }
  else if (above && arr[i] <= lower)
   above = false;
 }
 if (crossingCount < 2)
  return 0;
 cal:
 float totalSamples = 0;
 for (int i = 1; i < crossingCount; i++)
  totalSamples += crossings[i] - crossings[i - 1];
 float averageSamplesPerCycle = totalSamples / (crossingCount - 1);
 if (averageSamplesPerCycle <= 0)
  return 0;
 float period = averageSamplesPerCycle * actualSampleInterval;
 return 1000000.0 / period;
}

void calculateComparison()
{
 float deltaVAvg = 0;
 float deltaVRMS = 0;
 float frequency1 = frequency;
 float frequency2 = calculateFrequency(sample2);
 float frequencyDifference = frequency2 - frequency1;
 sameFrequency = false;
 if (frequency1 > 0.1 && frequency2 > 0.1 && abs(frequency2 - frequency1) < frequency1 * 0.5)
  sameFrequency = true;
 else
  sameFrequency = false;
 float vPP1 = vPP;
 float vAvg1 = vAvg;
 float vRMS1 = vRMS;
 ch1IsSine = chIsSine;
 waveAnalysis(sample2);
 float vPP2 = vPP;
 float vAvg2 = vAvg;
 float vRMS2 = vRMS;
 float gain = 0; 
 ch2IsSine = chIsSine;
 if (sameFrequency && vPP1 > 0.01)
 {
  gain = vPP2 / vPP1;
  if (gain > 0)
   gainDB = 20.0 * log10(gain);
  else
   gainDB = 0;
 }
 else
  gainDB = 0;
 deltaVAvg = vAvg2 - vAvg1;
 deltaVRMS = vRMS2 - vRMS1;
 updateValue(deltaVAvg, oldValues[23], 40, 90, 1, 7, 1,ST77XX_YELLOW);
 updateValue(deltaVRMS, oldValues[24], 40, 99, 1, 7, 1,ST77XX_YELLOW);
}

void phaseDifference(uint16_t *arr1, uint16_t *arr2)
{
 int min1 = 1023;
 int min2 = 1023;
 int max1 = 0;
 int max2 = 0;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (arr1[i] < min1) min1 = arr1[i];
  if (arr1[i] > max1) max1 = arr1[i];
  if (arr2[i] < min2) min2 = arr2[i];
  if (arr2[i] > max2) max2 = arr2[i];
 }
 int mid1 = (min1 + max1) / 2;
 int mid2 = (min2 + max2) / 2;
 int peakStart1 = -1;
 int peakEnd1 = -1;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (arr1[i] > mid1)
  {
   if (arr1[i] >= max1 - 40)
   {
    peakStart1 = i;
    break;
   }
  }
 }
 if (peakStart1 >= 0)
 {
  peakEnd1 = peakStart1;
  while (peakEnd1 + 1 < SAMPLES && arr1[peakEnd1 + 1] >= max1 - 40)
  {
   peakEnd1++;
  }
  if(ch1IsSine == true)
   peak1 = (peakStart1 + peakEnd1) / 2.0;
  else
   peak1 = peakStart1;
 }
 int peakStart2 = -1;
 int peakEnd2 = -1;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (arr2[i] > mid2)
  {
   if (arr2[i] >= max2 - 40)
   {
    peakStart2 = i;
    break;
   }
  }
 }
 if (peakStart2 >= 0)
 {
  peakEnd2 = peakStart2;
  while (peakEnd2 + 1 < SAMPLES && arr2[peakEnd2 + 1] >= max2 - 40)
  {
   peakEnd2++;
  }
  if(ch2IsSine == true)
   peak2 = (peakStart2 + peakEnd2) / 2.0;
  else
   peak2 = peakStart2;
 }
 if (sameFrequency && frequency > 0)
 {
  phaseDiff = (float)(peak1 - peak2) * actualSampleInterval * frequency * 360.0 / 1000000.0;
  if(abs(phaseDiff)>180.0)
   phaseDiff = -1.0 * sign(phaseDiff) * (360.0 - abs(phaseDiff));
  Delay = 1000.0 * phaseDiff/(360.0 * frequency);
 }
 else
 {
  phaseDiff = 0;
  Delay = 0;
 }
 calculateCorrelation(sample1, sample2);
 if(ch1IsSine && ch2IsSine)
 {
  phaseDiff = sign(phaseDiff) * acos(cor) * 180.0 / PI;
  Delay = 1000.0 * phaseDiff/(360.0 * frequency);
  tft.fillRect(0,68,128,19,ST77XX_BLACK);
  tft.drawFastVLine(min(peak1,peak2),69,7,ST77XX_WHITE);
  tft.drawFastVLine(max(peak1,peak2),69,7,ST77XX_WHITE);
  drawArrow(min(peak1,peak2)-15, 72, 13, 1, ST77XX_WHITE);
  drawArrow(max(peak1,peak2)+15, 72, 13, -1, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE);
  int mid = max((int)(min(peak1,peak2) + max(peak1,peak2))/2.0 - 5,0);
  if(phaseDiff < 0)
   tft.setCursor(max(mid-5,0),79);
  else
   tft.setCursor(mid,79);
  tft.print(phaseDiff,0);
  tft.print((char)248);
 }
 oldValues[20] = phaseDiff;
}

void printDelta(int x, int y)
{
 tft.drawLine(x, y+5, x+4, y+0, ST77XX_WHITE);
 tft.drawLine(x+4, y+0, x+8, y+5, ST77XX_WHITE);
 tft.drawLine(x, y+5, x+8, y+5, ST77XX_WHITE);
}

void calculateRiseFallTime(uint16_t *arr)
{
 int minADC = 1023;
 int maxADC = 0;
 for (int i = 0; i < SAMPLES; i++)
 {
  if (arr[i] < minADC)
   minADC = arr[i];
  if (arr[i] > maxADC)
   maxADC = arr[i];
 }
 int amplitude = maxADC - minADC;
 if (amplitude < 10)
 {
  riseTime = 0;
  fallTime = 0;
  return;
 }
 float level10 = minADC + 0.10 * amplitude;
 float level90 = minADC + 0.90 * amplitude;
 float rise10 = -1;
 float rise90 = -1;
 float fall90 = -1;
 float fall10 = -1;
 for (int i = 1; i < SAMPLES; i++)
 {
  if (rise10 < 0 && arr[i - 1] < level10 && arr[i] >= level10)
  {
   rise10 = (i - 1) + (level10 - arr[i - 1]) / (float)(arr[i] - arr[i - 1]);
  }
  if (rise10 >= 0 && rise90 < 0 && arr[i - 1] < level90 && arr[i] >= level90)
  {
   rise90 = (i - 1) + (level90 - arr[i - 1]) /(float)(arr[i] - arr[i - 1]);
   break;
  }
 }
 for (int i = 1; i < SAMPLES; i++)
 {
  if (fall90 < 0 && arr[i - 1] > level90 && arr[i] <= level90)
  {
   fall90 = (i - 1) + (level90 - arr[i - 1]) / (float)(arr[i] - arr[i - 1]);
  }
  if (fall90 >= 0 && fall10 < 0 && arr[i - 1] > level10 && arr[i] <= level10)
  {
   fall10 = (i - 1) + (level10 - arr[i - 1]) / (float)(arr[i] - arr[i - 1]);
   break;
  }
 }
 if (rise10 >= 0 && rise90 >= 0)
  riseTime = abs((rise90 - rise10) * actualSampleInterval/1000.0);
 else
  riseTime = 0;
 if (fall90 >= 0 && fall10 >= 0)
  fallTime = abs((fall10 - fall90) * actualSampleInterval/1000.0);
 else
  fallTime = 0;
}

void calculateTHD(uint16_t *arr)
{
 uint16_t harmonicArray[15];
 float thdPercent = 0;
 for(uint8_t h=0; h<15; h++)
  harmonicArray[h] = 0;
 if (frequency < 0.1)
  goto showTHD;
 float sampleIntervalS = actualSampleInterval * 0.000001;
 float samplesPerS = 1.0 / sampleIntervalS;
 if (frequency >= samplesPerS / 2.0)
  goto showTHD;
 waveAnalysis(arr);
 float average = 0;
 for (int i = 0; i < endPoint; i++)
  average += arr[i];
 average /= endPoint;
 long fundamentalReal = 0;
 long fundamentalImag = 0;
 for (int n = 0; n < endPoint; n++)
 {
  float angle = 2.0 * PI * frequency * n / samplesPerS;
  float sample = arr[n] - average;
  fundamentalReal += (long)(sample * cos(angle));
  fundamentalImag += (long)(sample * sin(angle));
 }
 float fundamental = (2.0 / endPoint) * sqrt(fundamentalReal * fundamentalReal + fundamentalImag * fundamentalImag);
 harmonicArray[0] = fundamental;
 if(fundamental <= 0.001)
  goto showTHD;
 long harmonicSum = 0;
 for (int h = 2; h <= 15; h++)
 {
  float harmonicFrequency = frequency * h;
  if (harmonicFrequency >= samplesPerS / 2.0)
   break;
  long realPart = 0;
  long imagPart = 0;
  for (int n = 0; n < endPoint; n++)
  {
   float angle = 2.0 * PI * harmonicFrequency * n / samplesPerS;
   float sample = arr[n] - average;
   realPart += (long)(sample * cos(angle));
   imagPart += (long)(sample * sin(angle));
  }
  float harmonic = (2.0 / endPoint) * sqrt(realPart * realPart + imagPart * imagPart);
  harmonicArray[h-1] = harmonic;
  if(harmonic > fundamental * 0.02)
   harmonicSum += harmonic * harmonic;
 }
 thdPercent = sqrt(harmonicSum) / fundamental;
 if(thdPercent >9.99)
  thdPercent = 9.99;
 thdPercent *= 100.0;
 showTHD:
 tft.setCursor(14,2);
 tft.print(F("HARMONIC ANALYSIS"));
 tft.drawRect(0,22,128,125,ST77XX_RED);
 tft.drawFastVLine(20,22,125,ST77XX_RED);
 tft.setCursor(38,12);
 if(currentWindow == 3)
 {
  tft.setTextColor(ST77XX_GREEN);
  tft.print(F("CHANNEL-1"));
 }
 else
 {
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(F("CHANNEL-2"));
 }
 tft.setTextColor(ST77XX_WHITE);
 for(int y=0; y < 12; y++)
 {
  tft.setCursor(8, y*10 + 26);
  if(y >= 9)
   tft.setCursor(2, y*10 + 26);
  tft.print(F("H"));
  tft.print(y+1);
  uint16_t t1 = min(harmonicArrayOld[y],512)/7.5;
  uint16_t t2 = min(harmonicArray[y],512)/7.5;
  if(t2 > t1)
   tft.fillRect(22+t1,y*10 + 26,t2-t1,8,ST77XX_BLUE);
  else
   tft.fillRect(22+t2,y*10 + 26,t1-t2,8,ST77XX_BLACK);
  tft.setCursor(96, y*10 + 26);
  if(abs(floor(harmonicArray[y]*100*5.0/1023)-floor(harmonicArrayOld[y]*100*5.0/1023))>0)
   tft.fillRect(96,y*10 + 26,23,10,ST77XX_BLACK);
  tft.print(floor(harmonicArray[y]*100*5.0/1023)/100.0,2);
  tft.print(F("V"));
 }
 tft.setCursor(2,150);
 tft.print(F("THD:"));
 if(abs(round(thdPercent)-round(oldValues[17])) > 0)
 {
  tft.fillRect(27,150,29,7, ST77XX_BLACK);
  tft.setCursor(27,150);
  tft.print(floor(thdPercent),0);
  tft.print(F("%"));
 }
 oldValues[17] = thdPercent;
 for(uint8_t h=0; h<12; h++)
  harmonicArrayOld[h] = harmonicArray[h];
 displayText(22,62,150);
 if(round(frequency)!=round(oldValues[14]))
  tft.fillRect(92,150,36,7,ST77XX_BLACK);
 oldValues[14] = frequency;
 tft.print(frequency,0);
 tft.print(F("Hz"));
}

void setBestADCPrescaler(float signalFreq)
{
 if (signalFreq <= 0)
  return;
 const float Fs_required = signalFreq * 64.0f * 4.0f;
 const float requiredADCclock = Fs_required * 13.0f * 1.20f;
 uint8_t setting;
 if (F_CPU / 16.0f >= requiredADCclock)
  setting = 0x04;
 else if (F_CPU / 8.0f >= requiredADCclock)
  setting = 0x03;
 else if (F_CPU / 4.0f >= requiredADCclock)
  setting = 0x02;
 else
  setting = 0x02;
 ADCSRA = (ADCSRA & 0xF8) | setting;
}

void updateValue(float v, float &old, int x, int y, byte decimalDigits, byte len, byte unit, uint16_t color)
{
 tft.setTextColor(color);
 int scale = decimalDigits == 2 ? 100 : decimalDigits == 1 ? 10 : 1;
 if (round(v * scale) != round(old * scale))
 {
  tft.fillRect(x,y,len*6-1,7,ST77XX_BLACK);
  tft.setCursor(x,y);
  byte k;
  if(unit == 2 || unit == 3 || unit == 5) k = 2;
  else if (unit == 1 || unit == 4) k = 1;
  else k = 0;
  int p = len - decimalDigits - min(decimalDigits,1) - k;
  if(v < 0) p--;
  if(abs(v) < pow(10,p))
  {
   if(unit == 2)
    len = 5;
   uint16_t n = abs(round(v*scale))/scale;
   byte digits = n < 10 ? 1 : n < 100 ? 2 : n < 1000 ? 3 : 4;
   if(v < 0)
    digits ++;
   int s = len - digits - decimalDigits - min(decimalDigits,1) - k;
   if(s > 0)
    for(uint8_t i=0; i < s; i++)
     tft.print(F(" "));
   tft.print(v,decimalDigits);
   if(unit==1) tft.print(F("V"));
   else if(unit==2) tft.print(F("Hz"));
   else if(unit==3) tft.print(F("dB"));
   else if(unit==4) tft.print(F("%"));
   else if(unit==5) tft.print(F("ms"));
  }
  else
  {
   for(uint8_t i=3; i<len; i++)
    tft.print(F(" "));
   tft.print(F("Inf"));
  }
  old=v;
 }
}

int8_t sign(float v)
{
 if(v < 0)
  return -1;
 else
  return 1;
}

void displayRiseFallTime()
{
 tft.setTextColor(ST77XX_WHITE);
 tft.setCursor(0,70);
 tft.print(F("Rise Time:"));
 tft.setCursor(0,79);
 tft.print(F("Fall Time:"));
 tft.setCursor(116,70);
 tft.print(F("ms"));
 tft.setCursor(116,79);
 tft.print(F("ms"));
 calculateRiseFallTime(sample1);
 if(riseTime < 100)
  updateValue(riseTime, oldValues[25], 60, 70, 1, 3, 0,ST77XX_GREEN);
 else
  updateValue(riseTime, oldValues[25], 60, 70, 0, 3, 0,ST77XX_GREEN);
 if(fallTime < 100)
  updateValue(fallTime, oldValues[26], 60, 79, 1, 3, 0,ST77XX_GREEN);
 else
  updateValue(fallTime, oldValues[26], 60, 79, 0, 3, 0,ST77XX_GREEN);
 calculateRiseFallTime(sample2);
 if(riseTime < 100)
  updateValue(riseTime, oldValues[27], 90, 70, 1, 3, 0,ST77XX_YELLOW);
 else
  updateValue(riseTime, oldValues[27], 90, 70, 0, 3, 0,ST77XX_YELLOW);
 if(fallTime < 100)
  updateValue(fallTime, oldValues[28], 90, 79, 1, 3, 0,ST77XX_YELLOW);
 else
  updateValue(fallTime, oldValues[28], 90, 79, 0, 3, 0,ST77XX_YELLOW);
}

void calculateCorrelation(uint16_t *arr1, uint16_t *arr2)
{
 float avgA = 0, avgB = 0;
 float squareSumA = 0, squareSumB = 0;
 for (int i = 0; i < endPointCh1; i++)
 {
  avgA += arr1[i];
  avgB += arr2[i];
 }
 avgA /= endPointCh1;
 avgB /= endPointCh1;
 cor = 0;
 for (int i = 0; i < endPointCh1; i++)
 {
  float x = arr1[i] - avgA;
  float y = arr2[i] - avgB;
  cor  += x * y;
  squareSumA += x * x;
  squareSumB += y * y;
 }
 if (squareSumA == 0 || squareSumB == 0)
  cor = 0;
 else
  cor = cor / sqrt(squareSumA * squareSumB);
 cor = constrain(cor, -1.0, 1.0);
 float phase = acos(cor);
 float adcDelay = (13.0 * prescaler * 1000000.0) / F_CPU;
 float phaseError = 2.0 * 2.0 * PI * frequency * adcDelay / 1000000.0;
 phase -= sign(phaseDiff) * phaseError;
 if(phase < 0) phase = 0; 
 cor = cos(phase);
 updateValue(cor, oldValues[11], 40, 126, 2, 7, 0,ST77XX_YELLOW); 
}

void calculateDRMS(uint16_t *arr1, uint16_t *arr2)
{
 float sum = 0;
 for (int i = 0; i < SAMPLES; i++)
 {
  float d = ((int)arr2[i] - (int)arr1[i]) * 5.0 / 1023.0;
  sum += d * d;
 }
 float drms = sqrt(sum / SAMPLES);
 updateValue(drms, oldValues[12], 40, 135, 2, 7, 1,ST77XX_YELLOW);
}

void calculateVDPP(uint16_t *arr1, uint16_t *arr2)
{
 int minD = 1023;
 int maxD = -1023;
 for (int i = 0; i < SAMPLES; i++)
 {
  int d = (int)arr2[i] - (int)arr1[i];
  if (d < minD) minD = d;
  if (d > maxD) maxD = d;
 }
 float vDPP = (maxD - minD) * 5.0 / 1023.0;
 updateValue(vDPP, oldValues[13], 40, 144, 2, 7, 1,ST77XX_YELLOW);
}

void displayText(uint8_t t,uint8_t x, uint8_t y)
{
  char text[]="xxxxxx";
  strcpy_P(text, pName[t]);
  tft.setCursor(x,y);
  tft.print(text);
}
