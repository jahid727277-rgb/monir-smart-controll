#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <FirebaseESP8266.h>
#include <time.h>

#define FIREBASE_HOST "https://monir-smart-control-default-rtdb.firebaseio.com"
#define FIREBASE_SECRET "gTiMlZxe9Sv5H1Yo6pz85ZJQNIchwWIqjThzDAX2"

#define ALARM_PIN D1   // ✅ D1 pin

FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;

// ---------- alarm control ----------
bool alarmOn = false;
unsigned long alarmStart = 0;
unsigned long alarmDurationMs = 0;

unsigned long lastCheck = 0;
const unsigned long CHECK_INTERVAL = 1000;

int lockedMinute = -1;

// ---------- time parser ----------
void parseTime(String t, int &h, int &m) {
  int sp = t.indexOf(' ');
  String timePart = t.substring(0, sp);
  String ampm = t.substring(sp + 1);

  int c = timePart.indexOf(':');
  h = timePart.substring(0, c).toInt();
  m = timePart.substring(c + 1).toInt();

  if (ampm == "PM" && h != 12) h += 12;
  if (ampm == "AM" && h == 12) h = 0;
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW);

  WiFiManager wm;
  wm.autoConnect("Alarm-Setup");

  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_SECRET;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  configTime(6 * 3600, 0, "pool.ntp.org");
  while (time(nullptr) < 10000) delay(500);

  Serial.println("Alarm system READY ✅");
}

// ---------- loop ----------
void loop() {

  // --------- turn alarm OFF ---------
  if (alarmOn && millis() - alarmStart >= alarmDurationMs) {
    digitalWrite(ALARM_PIN, LOW);
    alarmOn = false;
    Serial.println("🔕 Alarm OFF");
  }

  // --------- check every second ---------
  if (millis() - lastCheck < CHECK_INTERVAL) return;
  lastCheck = millis();

  time_t now = time(nullptr);
  struct tm *t = localtime(&now);

  int curH = t->tm_hour;
  int curM = t->tm_min;
  int today = t->tm_wday;

  if (curM == lockedMinute) return;
  if (!Firebase.ready()) return;

  if (!Firebase.getJSON(fbdo, "/alarms")) return;

  FirebaseJson alarms = fbdo.jsonObject();
  size_t count = alarms.iteratorBegin();

  for (size_t i = 0; i < count; i++) {

    int type;
    String key, raw;
    alarms.iteratorGet(i, type, key, raw);

    FirebaseJson alarm;
    alarm.setJsonData(raw);
    FirebaseJsonData d;

    // ---- active ----
    alarm.get(d, "active");
    if (!d.success || !d.to<bool>()) continue;

    // ---- time ----
    alarm.get(d, "time");
    int ah, am;
    parseTime(d.to<String>(), ah, am);

    // ---- duration ----
    alarm.get(d, "duration");
    if (!d.success) continue;
    alarmDurationMs = d.to<int>() * 1000UL;   // ✅ seconds → ms

    // ---- days array ----
    alarm.get(d, "days");
    FirebaseJsonArray daysArr;
    daysArr.setJsonArrayData(d.to<String>());

    bool dayMatch = false;
    for (int x = 0; x < daysArr.size(); x++) {
      FirebaseJsonData v;
      daysArr.get(v, x);
      if (v.to<int>() == today) {
        dayMatch = true;
        break;
      }
    }
    if (!dayMatch) continue;

    // ---- FINAL MATCH ----
    if (ah == curH && am == curM) {
      digitalWrite(ALARM_PIN, HIGH);
      alarmOn = true;
      alarmStart = millis();
      lockedMinute = curM;

      Serial.print("🔔 Alarm ON for ");
      Serial.print(alarmDurationMs / 1000);
      Serial.println(" seconds");

      break;
    }
  }

  alarms.iteratorEnd();
}
