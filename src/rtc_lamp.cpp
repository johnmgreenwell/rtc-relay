
/* ---------------------------------------
* RTC Lamp Code
* ----------------------------------------
* John Greenwell, 2014
* ----------------------------------------
*/

#include <Arduino.h>
#include <Time.h>
#include <SoftwareSerial.h>
#include <TinyWireM.h>
#include <USI_TWI_Master.h>
#include <DS1307RTC_Tiny.h>


// Debug Setting
#define DEBUG_MODE  0
#define SET_TIME    0

// Variable declaration
char relay = 1;  // Relay pin number
char rec = 0;
char cnt = 0;
char digit[2] = {0,0};
char finalDigit = 0;
char Wday = 1;
char mem[3] = {0,7,0};
bool relay_t = 0;
bool err = FALSE;
bool run = FALSE;
bool inProcess = TRUE;
bool recentlyFlipped = FALSE;
SoftwareSerial mySerial(3, 4); // RX, TX
tmElements_t tm;
tmElements_t alm[7];  // Variables for day-of-week alarms
bool dayIsSet[7] = {0,0,0,0,0,0,0};
const char dayName[7] = {'S','M','T','W','R','F','S'};

// the setup routine runs once when you press reset:
void setup() {                
  
  // initialize the digital pin as an output.
  pinMode(relay, OUTPUT);
  digitalWrite(relay, LOW);   // The relay should begin off
  
#if SET_TIME==1

  tm.Second = 0;
  tm.Minute = 34; 
  tm.Hour = 6;
  tm.Day = 5;
  tm.Wday = 5;
  tm.Month = 12;
  tm.Year = CalendarYrToTm(2014);

  if (RTC.write(tm)) {
    run = TRUE;
  }
  
#endif
  
  // Delay a while for startup
  delay(100);

  // set the data rate for the SoftwareSerial port
  mySerial.begin(19200);
  delay(100);
  
  // Check if clock is already running
  if (updateTime()) { 
    run = TRUE;
  }

  // Populate alarm settings from RTC memory
  if (run) {
    for (char i=0;i<7;i++) {
      rtcReadMem(mem,0x08+(3*i));
      dayIsSet[i] = (bool)mem[0];
      alm[i].Hour = mem[1];
      alm[i].Minute = mem[2];
    }
  } else {  // For totally reset chip, populate generic
    for (char i=0;i<7;i++) {
      rtcWriteMem(mem,0x08+(3*i));
      alm[i].Hour = mem[1];
    }
  }
  
#if DEBUG_MODE == 1
  // Debug the shutdown conditions
  if (MCUSR & (1<<WDRF)) {
    mySerial.println("WDRF");
  } else if (MCUSR & (1<<BORF)) {
      mySerial.println("BORF");
    } else if (MCUSR & (1<<EXTRF)) {
        mySerial.println("EXTRF");
      } else if (MCUSR & (1<<PORF)) {
          mySerial.println("PORF");
        } else {
          mySerial.println("??");
        }
#endif
  
}

// the loop routine runs over and over again forever:
void loop() {

#if DEBUG_MODE==1

  if (RTC.read(tm)) {
    mySerial.print("Ok, Time = ");
    print2digits(tm.Hour);
    mySerial.write(':');
    print2digits(tm.Minute);
    mySerial.write(':');
    print2digits(tm.Second);
    mySerial.print(", Date (D/M/Y) = ");
    mySerial.print(tm.Day);
    mySerial.write('/');
    mySerial.print(tm.Month);
    mySerial.write('/');
    mySerial.print(tmYearToCalendar(tm.Year));
    mySerial.println();
    mySerial.print("Wday = ");
    mySerial.print(tm.Wday);
    mySerial.println();
  } else {
    if (RTC.chipPresent()) {
      mySerial.println("Clock is stopped.");
      mySerial.println();
    } else {
      mySerial.println("Read error.");
      mySerial.println();
    }
    delay(9000);
  }
  delay(2000);

#endif

  if (mySerial.available()) {
    processRequest();
  } else if (cnt >= 20) {
    cnt = 0;
    checkAlarm();
  } else {
    cnt++;
  }
  delay(250);

}

// FUNCTION DECLARATIONS

// Handle User Interface
void processRequest() {
  if (err) {
    mySerial.println("Er");
    return;
  }
  if (!run) {
    printClockNotSet();
  }
  rec = mySerial.read();
  while (inProcess) {
    if (rec == '?') {
      printStatus();
    } else if (rec == 'a') {
        inProcess = setAlarm();
      } else if (rec == 't') {
          inProcess = setNewTime();
          inProcess = FALSE;
        } else if (rec == '!') {
            relayToggle();
            printState(relay_t);
            mySerial.println();
            inProcess = FALSE;
          } else {
            inProcess = printInvalidEntry();
          }
    inProcess = FALSE;
  }
  
  inProcess = TRUE; 
  
}

// User select value
bool selectVal() {
  char time = 0;
  while (time < 200) {
    if (mySerial.available()) {
      rec = mySerial.read();
      return TRUE;
    }
    delay(200);
    time++;
  }
  return FALSE;
}

// Select digit function -- manipulates global rec
bool selectDigit() {
  if (selectVal()) {
    rec = rec - '0'; // This converts it to actual number
    if (rec > 9 || rec < 0) {
      return FALSE;
    } else {
      return TRUE;
    }
  } else {
    return FALSE;
  }
}

// Select 2 digit function -- manipulates global rec, digit, finaldigit
bool select2Digits() {
  // Get two digits and combine them
  for (char i=0;i<2;i++) {
    if (selectDigit()) {
      digit[i] = rec;
    } else if (rec + '0' == '*') {
      finalDigit = '*';
      return TRUE;
    } else {
      return FALSE;
    }
  }
  finalDigit = 10*digit[0] + digit[1];
  return TRUE;
}

// User set alarm
bool setAlarm() {
  printSelectWeekday();
  if (selectDigit()) {
    if (rec > 7 || rec < 1) {
      return printInvalidEntry();
    } else {
      alm[rec-1].Wday = rec;
      Wday = alm[rec-1].Wday-1;
      printSelected();
      mySerial.println(dayName[Wday]);
      printSelect2Digits(1);
      mySerial.print("Or * Set/");
      printUnset();
      if (select2Digits()) {
        if (finalDigit < 24 && finalDigit >= 0) {
          alm[Wday].Hour = finalDigit;
        } else if (finalDigit == '*') {
          dayIsSet[Wday] = !dayIsSet[Wday];
          mem[0] = (char)dayIsSet[Wday];
          mem[1] = alm[Wday].Hour;
          mem[2] = alm[Wday].Minute;
          rtcWriteMem(mem,0x08+(3*Wday));
          printStatus();
          return TRUE;
        } else {
          return printInvalidEntry();
        }
      } else {
        return printInvalidEntry();
      }
    }
  } else {
    return printInvalidEntry();
  }
  // This point reached only if none of previous fails
  printSelect2Digits(0);
  if (select2Digits()) {
    if (finalDigit < 60 && finalDigit >= 0) {
      alm[Wday].Minute = finalDigit;
    } else {
      return printInvalidEntry();
    }
  } else {
    return printInvalidEntry();
  }
  // Success point
  dayIsSet[Wday] = TRUE;
  mem[0] = (char)dayIsSet[Wday];
  mem[1] = alm[Wday].Hour;
  mem[2] = alm[Wday].Minute;
  rtcWriteMem(mem,0x08+(3*Wday));
  printStatus();
  return TRUE;
}

// Set the time
bool setNewTime() {
  printSelect2Digits(2);
  if (select2Digits()) {
    if (finalDigit < 32 && finalDigit > 0) {
      tm.Day = finalDigit;
    } else {
      return printInvalidEntry();
    }
  } else {
    return printInvalidEntry();
  }
  // This point reached only if none of previous fails
  printSelect2Digits(3);
  if (select2Digits()) {
    if (finalDigit < 13 && finalDigit > 0) {
      tm.Month = finalDigit;
    } else {
      return printInvalidEntry();
    }
  } else {
    return printInvalidEntry();
  }
  // This point reached only if none of previous fails
  printSelect2Digits(4);
  if (select2Digits()) {
    tm.Year = CalendarYrToTm(2000 + finalDigit);
  } else {
    return printInvalidEntry();
  }
  // This point reached only if none of previous fails
  printSelectWeekday();
  if (selectDigit()) {
    if (rec > 7 || rec < 1) {
      return printInvalidEntry();
    } else {
      tm.Wday = rec;
      printSelected();
      mySerial.println(dayName[tm.Wday-1]);
      printSelect2Digits(1);
      if (select2Digits()) {
        if (finalDigit < 24 && finalDigit >= 0) {
          tm.Hour = finalDigit;
        } else {
          return printInvalidEntry();
        }
      } else {
        return printInvalidEntry();
      }
    }
  } else {
    return printInvalidEntry();
  }
  // This point reached only if none of previous fails
  printSelect2Digits(0);
  if (select2Digits()) {
    if (finalDigit < 60 && finalDigit >= 0) {
      tm.Minute = finalDigit;
    } else {
      return printInvalidEntry();
    }
  } else {
    return printInvalidEntry();
  }
  // Success point
  RTC.write(tm);
  printStatus();
  return TRUE;
}

// Update Time
bool updateTime() {
  if (RTC.read(tm)) {
    err = FALSE;
    run = TRUE;
    //if (tm.Wday > 7) {
    //  tm.Wday = 1;
    //  RTC.write(tm);
    //}
    return TRUE;
  } else if (RTC.chipPresent()) {
    printClockNotSet();
    err = FALSE;
    run = FALSE;
    return FALSE;
  } else {
    err = TRUE;
    run = FALSE;
    return FALSE;
  }
}

// Check the time, compare to day's alarm
void checkAlarm() {
  Wday = tm.Wday-1;
  if (updateTime()) {
    if (!dayIsSet[Wday]) { return; }
    // Check alarm and toggle if Wday, hour, min match
    if (tm.Hour == alm[Wday].Hour) {
      if (tm.Minute == alm[Wday].Minute) {
        if (!recentlyFlipped) {
          relayToggle();
        }
        recentlyFlipped = TRUE;  // Just toggled
      } else {
        recentlyFlipped = FALSE; // Been at least a minute
      }
    // Turn the relay off an hour later
    } else if (tm.Hour == alm[Wday].Hour + 1) {
      recentlyFlipped = FALSE; // Could be 1 minute after toggle
      if (tm.Minute == alm[Wday].Minute) {
        if (relay_t) {
          relayOff();
        }
      }
    } else {
      recentlyFlipped = FALSE;  // Time has certainly elapsed
    }
  }
} 

// Print the status
void printStatus() {
  if (!updateTime()) { return; }
  print2digits(tm.Hour);
  printColon();
  print2digits(tm.Minute);
  printColon();
  print2digits(tm.Second);
  printSpace();
  mySerial.print("on");
  printSpace();
  mySerial.print(dayName[tm.Wday-1]);
  mySerial.print(",");
  printSpace();
  mySerial.print(tm.Day);
  mySerial.write('/');
  mySerial.print(tm.Month);
  mySerial.write('/');
  mySerial.print(tmYearToCalendar(tm.Year));
  mySerial.println();
  for (char j=0;j<21;j++) {
    mySerial.write('-');
  }
  mySerial.println();
  // Print all Wday Alarms
  for (char i=0;i<7;i++) {
    mySerial.print(dayName[i]);
    printColon();
    printSpace();
    printState(dayIsSet[i]);
    printSpace();
    print2digits(alm[i].Hour);
    printColon();
    print2digits(alm[i].Minute);
    mySerial.println();
  }
}  

// Toggle Relay
void relayToggle() {
  relay_t = !relay_t;
  digitalWrite(relay, relay_t);
}

// Turn Relay Off
void relayOff() {
  relay_t = FALSE;
  digitalWrite(relay, LOW);
}

// Function declaration
void print2digits(int number) {
  if (number >= 0 && number < 10) {
    mySerial.write('0');
  }
  mySerial.print(number);

}

// Messages only function: word unset w/newline
void printUnset() {
  mySerial.println("Unset");
}

// Massages only function: print space
void printSpace() {
  mySerial.write(' ');
}

// Messages only function: print ON or OFF
void printState(bool sel) {
  if (sel) {
    mySerial.print("ON");
  } else {
    mySerial.print("OFF");
  }
}

// Messages only function: clock not set
void printClockNotSet() {
  mySerial.print("Time ");
  printUnset();
}

// Messages only function: invalid entry
bool printInvalidEntry() {
  mySerial.println("Invalid entry.");
  return FALSE;
}

// Messages only function: select weekday
void printSelectWeekday() {
  mySerial.println("Enter wkday (1=Sun, 7=Sat).");
}

void printSelectDate() {
  mySerial.println("Date:");
}

// Messages only function: print selected
void printSelected() {
  mySerial.print("Chose: ");
}

// Messages only function: print a colon symbol
void printColon() {
  mySerial.write(':');
}

// Messages only function: select for choice
void printSelect2Digits(char sel) {
  mySerial.print("Enter 2 digits for ");
  if (!sel) {
    mySerial.print("min");
  } else if (sel==1) {
    mySerial.print("hour");
  } else if (sel==2) {
    mySerial.print("date");
  } else if (sel==3) {
    mySerial.print("month");
  } else {
    mySerial.print("year");
  }
  mySerial.println(" (i.e. 06 or 12...)");
}

// Aquire 3 bytes data from the RTC chip
char * rtcReadMem(char * var, char reg) { 
  
  // Address select
  TinyWireM.beginTransmission(0x68);
  TinyWireM.send(reg);
  TinyWireM.endTransmission();
  
  // Request the data field
  TinyWireM.requestFrom(0x68, 3);
  var[0] = TinyWireM.receive();
  var[1] = TinyWireM.receive();
  var[2] = TinyWireM.receive();

  return var;
}

// Write 3 bytes data to the RTC chip 
char * rtcWriteMem(char * var, char reg) {

  TinyWireM.beginTransmission(0x68);
  
  TinyWireM.send(reg); // reset register pointer 
  TinyWireM.send(var[0]);
  TinyWireM.send(var[1]); 
  TinyWireM.send(var[2]);
   
  TinyWireM.endTransmission();
}
