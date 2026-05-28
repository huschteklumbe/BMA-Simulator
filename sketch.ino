#include <LiquidCrystal.h>
#include <Wire.h>
#include <RTClib.h>

// BMA Simulator - Wokwi / Arduino Mega 2560
// GitHub -> Wokwi Import Version

LiquidCrystal lcd(22, 23, 24, 25, 26, 27);
RTC_DS1307 rtc;

// LEDs
#define LED_FBF_BETRIEB        2
#define LED_FBF_LOESCHANLAGE   3
#define LED_FBF_SIGNALE        4
#define LED_FBF_UE_AB          5
#define LED_FBF_UE_AUSGELOEST  6
#define LED_FBF_BRANDSTEUERUNG 7
#define LED_FBF_RUECKSETZEN    8
#define LED_FAT_BETRIEB        9
#define LED_FAT_ALARM          10
#define LED_FAT_STOERUNG       11
#define LED_FAT_ABSCHALTUNG    12
#define LED_FAT_EBENE          13
#define LED_FAT_SUMMER         44
#define LED_FAT_EBENE_AUF      45
#define LED_FAT_EBENE_AB       46

// Outputs
#define PIN_BUZZER             47
#define PIN_ALARMTROETE        48
#define PIN_BLITZ              49
#define PIN_RAUCH_OUT          50
#define PIN_DRUCK_OUT          51

// Inputs
#define BTN_TASTE1             30
#define BTN_TASTE2             31
#define BTN_TASTE3             32
#define BTN_TASTE4             33
#define BTN_EBENE_AUF          34
#define BTN_EBENE_AB           35
#define BTN_EBENE              36
#define BTN_SUMMER             37
#define BTN_BRANDSTEUERUNG     38
#define BTN_RESET              39
#define BTN_AKUSTIK            40
#define BTN_UE_AB              41
#define BTN_HANDDRUCK          42
#define BTN_RAUCH              43

enum SystemState {
  BOOT,
  STANDBY,
  ALARM,
  STOERUNG,
  ABSCHALTUNG,
  HISTORIE
};

SystemState state = BOOT;
SystemState returnState = STANDBY;

struct SoftTimer {
  unsigned long startMs = 0;
  unsigned long durationMs = 0;
  bool active = false;

  void start(unsigned long d) {
    startMs = millis();
    durationMs = d;
    active = true;
  }

  void stop() {
    active = false;
  }

  bool expired() {
    return active && (millis() - startMs >= durationMs);
  }

  bool running() {
    return active && !expired();
  }
};

struct Button {
  byte pin;
  bool lastStable = HIGH;
  bool lastReading = HIGH;
  unsigned long lastChange = 0;

  void begin(byte p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
  }

  bool pressed() {
    bool reading = digitalRead(pin);
    bool event = false;

    if (reading != lastReading) {
      lastChange = millis();
      lastReading = reading;
    }

    if ((millis() - lastChange) > 30) {
      if (reading != lastStable) {
        lastStable = reading;
        if (lastStable == LOW) event = true;
      }
    }

    return event;
  }

  bool held() {
    return digitalRead(pin) == LOW;
  }
};

Button bT1, bT2, bT3, bT4, bEbeneAuf, bEbeneAb, bEbene, bSummer, bBrand, bReset, bAkustik, bUeAb, bHand, bRauch;

struct EventEntry {
  char l1[21];
  char l2[21];
  char timeStamp[16];
};

EventEntry history[10];
int historyCount = 0;
int historyWrite = 0;
int historyView = -1;

bool alarmActive = false;
bool stoerungActive = false;
bool abschaltungActive = false;
bool muted = false;
bool akustikAus = false;
bool ueAb = false;
bool brandsteuerung = false;
bool alarmIsRauch = false;
bool alarmIsDruck = false;

SoftTimer bootTimer;
SoftTimer alarmSoundTimer;
SoftTimer stoerungInitialTimer;
SoftTimer stoerungPauseTimer;
SoftTimer stoerungBeepTimer;
SoftTimer abschaltungSoundTimer;
SoftTimer ruecksetzenNachlaufTimer;
SoftTimer alarmBlinkResetTimer;
SoftTimer fatEbeneTimer;
SoftTimer ueDelayTimer;
SoftTimer brandDelayTimer;
SoftTimer ebeneAufHoldTimer;

bool uePendingToggle = false;
bool brandPendingToggle = false;
bool resetBlinkActive = false;
bool historyMode = false;
bool ebeneAufHoldStarted = false;

unsigned long lastStandbyRefresh = 0;
unsigned long lastSoundToggle = 0;
bool sirenHi = false;

char currentLine1[21] = "";
char currentLine2[21] = "";
char currentLine3[21] = "";
char currentLine4[21] = "";

void setupPins() {
  int leds[] = {
    LED_FBF_BETRIEB, LED_FBF_LOESCHANLAGE, LED_FBF_SIGNALE, LED_FBF_UE_AB,
    LED_FBF_UE_AUSGELOEST, LED_FBF_BRANDSTEUERUNG, LED_FBF_RUECKSETZEN,
    LED_FAT_BETRIEB, LED_FAT_ALARM, LED_FAT_STOERUNG, LED_FAT_ABSCHALTUNG,
    LED_FAT_EBENE, LED_FAT_SUMMER, LED_FAT_EBENE_AUF, LED_FAT_EBENE_AB,
    PIN_ALARMTROETE, PIN_BLITZ, PIN_RAUCH_OUT, PIN_DRUCK_OUT
  };

  for (byte i = 0; i < sizeof(leds) / sizeof(leds[0]); i++) {
    pinMode(leds[i], OUTPUT);
    digitalWrite(leds[i], LOW);
  }

  pinMode(PIN_BUZZER, OUTPUT);

  bT1.begin(BTN_TASTE1);
  bT2.begin(BTN_TASTE2);
  bT3.begin(BTN_TASTE3);
  bT4.begin(BTN_TASTE4);
  bEbeneAuf.begin(BTN_EBENE_AUF);
  bEbeneAb.begin(BTN_EBENE_AB);
  bEbene.begin(BTN_EBENE);
  bSummer.begin(BTN_SUMMER);
  bBrand.begin(BTN_BRANDSTEUERUNG);
  bReset.begin(BTN_RESET);
  bAkustik.begin(BTN_AKUSTIK);
  bUeAb.begin(BTN_UE_AB);
  bHand.begin(BTN_HANDDRUCK);
  bRauch.begin(BTN_RAUCH);
}

void printPadded(byte row, const char* text) {
  lcd.setCursor(0, row);
  char buf[21];
  snprintf(buf, sizeof(buf), "%-20s", text);
  lcd.print(buf);
}

void nowStamp(char* buffer, byte len) {
  DateTime n = rtc.now();
  snprintf(buffer, len, "%02d.%02d %02d:%02d", n.day(), n.month(), n.hour(), n.minute());
}

void nowDateTimeLine(char* buffer, byte len) {
  DateTime n = rtc.now();
  snprintf(buffer, len, "%02d.%02d %02d:%02d", n.day(), n.month(), n.hour(), n.minute());
}

void addHistory(const char* l1, const char* l2) {
  strncpy(history[historyWrite].l1, l1, 20);
  strncpy(history[historyWrite].l2, l2, 20);
  history[historyWrite].l1[20] = 0;
  history[historyWrite].l2[20] = 0;
  nowStamp(history[historyWrite].timeStamp, sizeof(history[historyWrite].timeStamp));

  historyWrite = (historyWrite + 1) % 10;
  if (historyCount < 10) historyCount++;
}

void showBoot() {
  printPadded(0, "BMA - Simulator");
  printPadded(1, "FF Oberzent");
  printPadded(2, "FF Reichelsheim");
  printPadded(3, "GAERTNER/KAFFENB.");
}

void showStandby() {
  char dt[16];
  nowDateTimeLine(dt, sizeof(dt));
  printPadded(0, "BMA SIMULATOR");
  printPadded(1, "-Betriebsbereit-");
  printPadded(2, "--------------------");
  printPadded(3, dt);
}

void showCurrentAlarmText() {
  printPadded(0, currentLine1);
  printPadded(1, currentLine2);
  printPadded(2, currentLine3);
  printPadded(3, currentLine4);
}

void showHistory() {
  if (historyCount == 0 || historyView < 0) {
    printPadded(0, "HISTORIE");
    printPadded(1, "Keine Meldungen");
    printPadded(2, "");
    printPadded(3, "");
    return;
  }

  int idx = (historyWrite - 1 - historyView + 10) % 10;
  printPadded(0, "HISTORIE");
  printPadded(1, history[idx].l1);
  printPadded(2, history[idx].l2);
  printPadded(3, history[idx].timeStamp);
}

void clearAlarmOutputs() {
  digitalWrite(PIN_ALARMTROETE, LOW);
  digitalWrite(PIN_BLITZ, LOW);
  digitalWrite(PIN_RAUCH_OUT, LOW);
  digitalWrite(PIN_DRUCK_OUT, LOW);
  digitalWrite(LED_FBF_LOESCHANLAGE, LOW);
  digitalWrite(LED_FBF_UE_AUSGELOEST, LOW);
  digitalWrite(LED_FAT_ALARM, LOW);
  noTone(PIN_BUZZER);
}

void startAlarm(const char* l1, const char* l2, const char* l3, const char* l4, bool loesch, bool druckOut, bool rauchOut) {
  alarmActive = true;
  stoerungActive = false;
  abschaltungActive = false;
  muted = false;
  alarmIsDruck = druckOut;
  alarmIsRauch = rauchOut;

  strncpy(currentLine1, l1, 20); currentLine1[20] = 0;
  strncpy(currentLine2, l2, 20); currentLine2[20] = 0;
  strncpy(currentLine3, l3, 20); currentLine3[20] = 0;
  strncpy(currentLine4, l4, 20); currentLine4[20] = 0;

  addHistory(l1, l2);
  showCurrentAlarmText();

  digitalWrite(LED_FAT_ALARM, HIGH);
  digitalWrite(LED_FBF_LOESCHANLAGE, loesch ? HIGH : LOW);
  if (!ueAb) digitalWrite(LED_FBF_UE_AUSGELOEST, HIGH);

  digitalWrite(PIN_BLITZ, HIGH);
  digitalWrite(PIN_ALARMTROETE, akustikAus ? LOW : HIGH);

  alarmSoundTimer.start(30000);
  fatEbeneTimer.start(300000);

  state = ALARM;
}

void startStoerung() {
  stoerungActive = true;
  muted = false;
  char t[16];
  nowStamp(t, sizeof(t));
  char l1[21];
  snprintf(l1, sizeof(l1), "01 STOERUNG %s", t + 6);

  strncpy(currentLine1, l1, 20); currentLine1[20] = 0;
  strncpy(currentLine2, "STOERUNG GRUPPE 04", 20); currentLine2[20] = 0;
  currentLine3[0] = 0;
  currentLine4[0] = 0;

  addHistory(currentLine1, currentLine2);
  showCurrentAlarmText();

  digitalWrite(LED_FAT_STOERUNG, HIGH);
  stoerungInitialTimer.start(15000);
  stoerungPauseTimer.start(120000);
  state = STOERUNG;
}

void startAbschaltung() {
  abschaltungActive = true;
  muted = false;
  char t[16];
  nowStamp(t, sizeof(t));
  char l1[21];
  snprintf(l1, sizeof(l1), "01 ABSCHALT %s", t + 6);

  strncpy(currentLine1, l1, 20); currentLine1[20] = 0;
  strncpy(currentLine2, "LAGERHALLE SPRINKL", 20); currentLine2[20] = 0;
  currentLine3[0] = 0;
  currentLine4[0] = 0;

  addHistory(currentLine1, currentLine2);
  showCurrentAlarmText();

  digitalWrite(LED_FAT_ABSCHALTUNG, HIGH);
  abschaltungSoundTimer.start(15000);
  state = ABSCHALTUNG;
}

void resetAlarm() {
  alarmActive = false;
  resetBlinkActive = true;
  alarmBlinkResetTimer.start(600000);
  ruecksetzenNachlaufTimer.start(300000);
  clearAlarmOutputs();
  state = STANDBY;
  lcd.clear();
}

void resetAllNonAlarm() {
  stoerungActive = false;
  abschaltungActive = false;
  digitalWrite(LED_FAT_STOERUNG, LOW);
  digitalWrite(LED_FAT_ABSCHALTUNG, LOW);
  noTone(PIN_BUZZER);
  state = STANDBY;
  lcd.clear();
}

void handleSound() {
  if (muted) {
    noTone(PIN_BUZZER);
    digitalWrite(LED_FAT_SUMMER, HIGH);
    return;
  }
  digitalWrite(LED_FAT_SUMMER, LOW);

  if (alarmActive) {
    if (!alarmSoundTimer.expired()) {
      tone(PIN_BUZZER, 1000);
    } else if (millis() - lastSoundToggle > 500) {
      lastSoundToggle = millis();
      sirenHi = !sirenHi;
      tone(PIN_BUZZER, sirenHi ? 1200 : 800);
    }
  } else if (stoerungActive) {
    if (!stoerungInitialTimer.expired()) {
      tone(PIN_BUZZER, 1000);
    } else {
      noTone(PIN_BUZZER);
      if (stoerungPauseTimer.expired()) {
        stoerungBeepTimer.start(5000);
        stoerungPauseTimer.start(120000);
      }
      if (stoerungBeepTimer.running()) tone(PIN_BUZZER, 1000);
    }
  } else if (abschaltungActive) {
    if (!abschaltungSoundTimer.expired()) tone(PIN_BUZZER, 1000);
    else noTone(PIN_BUZZER);
  } else {
    noTone(PIN_BUZZER);
  }
}

void handleBlinkAndDelayed() {
  bool bootBlink = state == BOOT && (millis() / 300) % 2;
  digitalWrite(LED_FBF_BETRIEB, state == BOOT ? bootBlink : HIGH);
  digitalWrite(LED_FAT_BETRIEB, state == BOOT ? bootBlink : HIGH);

  if (resetBlinkActive && !alarmBlinkResetTimer.expired()) {
    digitalWrite(LED_FAT_ALARM, (millis() / 1000) % 2);
  } else if (resetBlinkActive) {
    resetBlinkActive = false;
    digitalWrite(LED_FAT_ALARM, LOW);
  }

  if (ruecksetzenNachlaufTimer.running() || alarmActive) digitalWrite(LED_FBF_RUECKSETZEN, HIGH);
  else digitalWrite(LED_FBF_RUECKSETZEN, LOW);

  if (fatEbeneTimer.running()) {
    digitalWrite(LED_FAT_EBENE, (millis() % 5000) < 1000 ? HIGH : LOW);
  } else {
    digitalWrite(LED_FAT_EBENE, LOW);
  }

  if (alarmActive) {
    if (alarmIsDruck) digitalWrite(PIN_DRUCK_OUT, (millis() % 5000) < 1000 ? HIGH : LOW);
    if (alarmIsRauch) digitalWrite(PIN_RAUCH_OUT, (millis() % 5000) < 1000 ? HIGH : LOW);
    digitalWrite(PIN_ALARMTROETE, akustikAus ? LOW : HIGH);
  }

  if (uePendingToggle && ueDelayTimer.expired()) {
    ueAb = !ueAb;
    uePendingToggle = false;
    digitalWrite(LED_FBF_UE_AB, ueAb ? HIGH : LOW);
  }

  if (brandPendingToggle && brandDelayTimer.expired()) {
    brandsteuerung = !brandsteuerung;
    brandPendingToggle = false;
    digitalWrite(LED_FBF_BRANDSTEUERUNG, brandsteuerung ? HIGH : LOW);
  }
}

void handleCommonButtons() {
  if (bSummer.pressed()) muted = !muted;

  if (bAkustik.pressed()) {
    akustikAus = !akustikAus;
    digitalWrite(LED_FBF_SIGNALE, akustikAus ? HIGH : LOW);
  }

  if (bUeAb.pressed()) {
    uePendingToggle = true;
    ueDelayTimer.start(ueAb ? 4000 : 2000);
  }

  if (bBrand.pressed()) {
    brandPendingToggle = true;
    brandDelayTimer.start(brandsteuerung ? 1000 : 5000);
  }

  digitalWrite(LED_FAT_EBENE_AUF, bEbeneAuf.held() ? HIGH : LOW);
  digitalWrite(LED_FAT_EBENE_AB, bEbeneAb.held() ? HIGH : LOW);
}

void enterHistory() {
  if (historyCount == 0) return;
  returnState = state;
  state = HISTORIE;
  historyMode = true;
  historyView = 0;
  lcd.clear();
  showHistory();
}

void exitHistory() {
  historyMode = false;
  state = returnState;
  lcd.clear();
  if (state == ALARM || state == STOERUNG || state == ABSCHALTUNG) showCurrentAlarmText();
}

void handleHistoryButton() {
  if (bEbeneAuf.held()) {
    if (!ebeneAufHoldStarted) {
      ebeneAufHoldStarted = true;
      ebeneAufHoldTimer.start(5000);
    } else if (ebeneAufHoldTimer.expired() && state != HISTORIE) {
      ebeneAufHoldStarted = false;
      enterHistory();
    }
  } else {
    ebeneAufHoldStarted = false;
  }

  if (state == HISTORIE) {
    if (bEbeneAb.pressed()) {
      historyView++;
      if (historyView >= historyCount) exitHistory();
      else showHistory();
    }

    if (bEbeneAuf.pressed()) {
      historyView--;
      if (historyView < 0) exitHistory();
      else showHistory();
    }
  }
}

void setup() {
  lcd.begin(20, 4);
  Wire.begin();
  rtc.begin();

  setupPins();

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  showBoot();
  bootTimer.start(20000);
  state = BOOT;
}

void loop() {
  handleCommonButtons();
  handleHistoryButton();
  handleSound();
  handleBlinkAndDelayed();

  if (state == HISTORIE) return;

  switch (state) {
    case BOOT:
      if (bootTimer.expired()) {
        lcd.clear();
        state = STANDBY;
      }
      break;

    case STANDBY:
      if (millis() - lastStandbyRefresh > 1000) {
        lastStandbyRefresh = millis();
        showStandby();
      }

      if (bT1.pressed()) startAlarm("05/10 ALARM FEUER", "SPRINKLER PROD.HALLE", "", "", true, false, false);
      if (bT2.pressed()) startStoerung();
      if (bT3.pressed()) startAbschaltung();
      if (bT4.pressed()) startAlarm("03/07 ALARM FEUER", "RAUCHMELDER LAGER", "03/09 ALARM FEUER", "RAUCHM. SERVERRAUM", false, true, false);
      if (bHand.pressed()) startAlarm("03/07 ALARM FEUER", "DRUCKMELDER KELLER", "", "", false, true, false);
      if (bRauch.pressed()) startAlarm("05/10 ALARM FEUER", "RAUCHM. FLUR BUERO", "", "", false, false, true);
      break;

    case ALARM:
      if (bReset.pressed()) resetAlarm();
      break;

    case STOERUNG:
      if (bT2.pressed()) resetAllNonAlarm();
      break;

    case ABSCHALTUNG:
      if (bT3.pressed()) resetAllNonAlarm();
      break;

    default:
      break;
  }
}
