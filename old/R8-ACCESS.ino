// -R8-ACCESS.ino = Exterior lights control based on PIR sensors, with manual override.
//
// 6nov26: Variable JSON_status for the Zarlar Dashboard (via Worker /sensor):
//         mw1/mw2/ms = motion in the last 10 min, lw1/lw2/ls = light on, au = automatic (PIR).
//         Light logic and Manual commands unchanged. E and N left out (move to ACCESS2/3).
// 3nov25: Remove homebridge commands,
// 21oct23: Improved with ChatGPT's help: Added timer
//
// ---------------------------------------
// PhotoniX shield v.3.0	I/O connections: (* = used)
//  A0 -
//  A1 -
// *A2 - PWM-E (Fader signal to 300 mA LED controller)
// *A3 - PWM-N (ON/OFF signal to 300 mA LED controller) = 12-bit DAC pin: PWM control not possible! (ATT! Not 5V tolerant!)
// *A4 - PWM-W2 (Fader signal to 300 mA LED controller)
// *A5 - PWM-W1 (Fader signal to 300 mA LED controller)
// *A6 - PWM-S (ON/OFF signal to 300 mA LED controller) = 12-bit DAC pin: PWM control not possible! (ATT! Not 5V tolerant!)
// *A7 - MOV-S (PIR sensor output)
// *D0/D1 - COM1/2 (I2C-SDA & SCL) = 16 pin expander
//  D2 -
// *D3 - MOV-W1 (PIR sensor output)
// *D4 - PIXEL-line (Optional: For status board?)
// *D5 - MOV-W2 (PIR sensor output)
// *D6 - MOV-E (PIR sensor output)
// *D7 - MOV-N (PIR sensor output)
//  TX/RX - COM6/7 = Serial comms
//  ---------------------------------------

// GENERAL settings:
STARTUP(WiFi.selectAntenna(ANT_AUTO)); // FAVORITE: continually switches at high speed between antennas (switched off if no ext antenna)
//SYSTEM_MODE(AUTOMATIC); // Needed?
//SYSTEM_THREAD(ENABLED); // User firmware runs also when not cloud connected. Allows mesh publish & subscribe code to continue even if gateway is not on-line or turned off.

boolean automatic = true; // Auto lights (PIR)

unsigned long lightStartTimeW1 = 0;
unsigned long lightStartTimeW2 = 0;
unsigned long lightStartTimeS = 0;
int autoLightDuration = 30000; // 30 seconds

// 6nov26: status for the Dashboard (Particle variable JSON_status, read by the Worker /sensor)
char JSON_status[100] = "{}";
const unsigned long MOTION_RECENT = 600000; // 10 min: "recent motion" (Dashboard polls every ~9 min)
unsigned long lastMotionW1 = 0, lastMotionW2 = 0, lastMotionS = 0;
bool seenW1 = false, seenW2 = false, seenS = false; // motion seen since boot
unsigned long lastStatus = 0;

int recent(bool seen, unsigned long last, unsigned long now)
{
  return (seen && now - last < MOTION_RECENT) ? 1 : 0;
}

void updateStatus(unsigned long now) // outputs are active LOW: LOW = light on
{
  snprintf(JSON_status, sizeof(JSON_status),
    "{\"mw1\":%d,\"mw2\":%d,\"ms\":%d,\"lw1\":%d,\"lw2\":%d,\"ls\":%d,\"au\":%d}",
    recent(seenW1, lastMotionW1, now), recent(seenW2, lastMotionW2, now), recent(seenS, lastMotionS, now),
    digitalRead(A5) == LOW ? 1 : 0, digitalRead(A4) == LOW ? 1 : 0, digitalRead(A6) == LOW ? 1 : 0,
    automatic ? 1 : 0);
}


void setup()
{
  // MOVs IN USE:
  pinMode(D3, INPUT_PULLDOWN); // MOV-W1
  pinMode(D5, INPUT_PULLDOWN); // MOV-W2
  pinMode(A7, INPUT_PULLDOWN); // MOV-S

  // LEDs IN USE:
  // *A5 W1 PWM pin (0-255)
  pinMode(A5, OUTPUT);
  digitalWrite(A5, 1);
  // *A4 W2 PWM pin (0-255)
  pinMode(A4, OUTPUT);
  digitalWrite(A4, 1);
  // *A6 S DAC pin (0-4095)
  pinMode(A6, OUTPUT);
  digitalWrite(A6, 1);

  // LEDs NOT YET IN USE:
  // *A2 E GPIO pin (Only digitalWrite!)
  pinMode(A2, OUTPUT);
  digitalWrite(A2, 1);
  // *A3 N DAC pin (0-4095)
  pinMode(A3, OUTPUT);
  digitalWrite(A3, 1);

  // GENERAL Particle functions:
  Particle.function("Manual", manual);
  Particle.variable("JSON_status", JSON_status); // 6nov26
  updateStatus(millis());
}



void loop()
{
  // 6nov26: remember motion for the status (also in manual mode; switches nothing)
  unsigned long now = millis();
  if (digitalRead(D3) == HIGH) { lastMotionW1 = now; seenW1 = true; }
  if (digitalRead(D5) == HIGH) { lastMotionW2 = now; seenW2 = true; }
  if (digitalRead(A7) == HIGH) { lastMotionS  = now; seenS  = true; }
  if (now - lastStatus >= 2000) { lastStatus = now; updateStatus(now); }

  if (automatic) // PIR sensed lights control
  {
    if (digitalRead(D3) == HIGH) // Motion detected MOV-W1
    {
      digitalWrite(A5, LOW); // Switch light W1 on
      digitalWrite(A4, LOW); // Switch light W2 on (Temporary until W2 PIR is repaired)
      lightStartTimeW1 = millis(); // Record the time
    }
    else // No motion detected MOV-W1
    {
      if (millis() - lightStartTimeW1 > autoLightDuration)
      {
        digitalWrite(A5, HIGH); // Switch light W1 off after duration
        digitalWrite(A4, HIGH); // Switch light W2 off (Temporary until W2 PIR is repaired)
      }
    }

    if (digitalRead(D5) == HIGH) // Motion detected MOV-W2
    {
      digitalWrite(A4, LOW); // Switch light W2 on
      lightStartTimeW2 = millis(); // Record the time
    }
    else // No motion detected MOV-W2
    {
      if (millis() - lightStartTimeW2 > autoLightDuration)
      {
        digitalWrite(A4, HIGH); // Switch light W2 off after duration
      }
    }

    if (digitalRead(A7) == HIGH) // Motion detected MOV-S
    {
      digitalWrite(A6, LOW); // Switch light S on
      lightStartTimeS = millis(); // Record the time
    }
    else // No motion detected MOV-S
    {
      if (millis() - lightStartTimeS > autoLightDuration)
      {
        digitalWrite(A6, HIGH); // Switch light S off after after duration
      }
    }
  }
}




// COMMON Particle Function(s):
int manual(String command) // = Particle.function to remote control manually. Can also be called from the loop(): ex = manual("Lighton");
{
  if(command == "W1on")
  {
    automatic = false; // Manual lights
    digitalWrite(A5, 0);
    return 200;
  }

  if(command == "W1off")
  {
    automatic = true; // PIR based lights
    digitalWrite(A5, 1);
    return 2;
  }

  if(command == "W2on")
  {
    automatic = false; // Manual lights
    digitalWrite(A4, 0);
    return 300;
  }

  if(command == "W2off")
  {
    automatic = true; // PIR based lights
    digitalWrite(A4, 1);
    return 3;
  }

  if(command == "Son") // S lights CONTROL: 12 bit DAC pin, no PWM!
  {
    automatic = false; // Manual lights
    digitalWrite(A6, 0);
    return 100;
  }

  if(command == "Soff") // S lights CONTROL: 12 bit DAC pin, no PWM!
  {
    automatic = true; // PIR based lights
    digitalWrite(A6, 1);
    return 1;
  }

  if(command == "Eon")
  {
    automatic = false; // Manual lights
    digitalWrite(A2, 0);
    return 400;
  }

  if(command == "Eoff")
  {
    automatic = true; // PIR based lights
    digitalWrite(A2, 1);
    return 4;
  }

  if(command == "Non") // N lights CONTROL: 12 bit DAC pin, no PWM!
  {
    automatic = false; // Manual lights
    digitalWrite(A3, 0);
    return 500;
  }

  if(command == "Noff") // N lights CONTROL: 12 bit DAC pin, no PWM!
  {
    automatic = true; // PIR based lights
    digitalWrite(A3, 1);
    return 5;
  }

  if(command == "reset") // You can remotely RESET the photon with this command...
  {
    System.reset();
    return -10000;
  }

  return -1;// If none above
}
