// mqtt_publisher.ino - publish the board's sensor readings to an MQTT broker.
//
// This is the "Night 2" firmware half of the plantfloor telemetry pipeline: instead
// of a PC-side bridge polling the board's HTTP API, the board publishes for itself.
// It is OFF by default. With no broker configured (mqttOn false or mqttHost blank) the
// board behaves exactly as it did before this file existed; nothing here runs.
//
// What it publishes (same topics, keys, order, types, and retained flag as the old bridge,
// so the historian and OPC UA server keep working unchanged). NOT byte-identical: the board
// formats numbers with fixed decimals and stamps ts at seconds precision, so the raw bytes
// differ from the bridge's JS output (25.0 vs 25; no milliseconds). Every value still parses
// identically and neither consumer (raw-text historian, tolerant JSON.parse in OPC UA) cares:
//   plantfloor/matrix/temperature   {"celsius":<n.1>,"ts":"<ISO8601 UTC seconds>"}   retained
//   plantfloor/matrix/accelerometer {"ax":<n.3>,"ay":<n.3>,"az":<n.3>,"ts":"..."}    retained
//
// Plus a birth/will pair on a DIFFERENT topic, on purpose:
//   plantfloor/status/matrix        {"online":true}  on connect (retained)
//                                   {"online":false} as the last will (retained)
// It is OUTSIDE plantfloor/matrix/# so the two subscribers (both listen to that
// wildcard) never see it: a status message inside that tree would add junk rows to the
// historian and would set the OPC UA server's shared ts to undefined.
//
// The "will" (Last Will and Testament) is a dead-man's switch: we hand it to the broker
// when we connect, and the broker publishes it for us if we drop off without a clean
// goodbye (power loss, WiFi drop, crash). It is a claim about DATA REACHABILITY, not
// hardware health: "the broker is no longer hearing from this board." Because it is
// retained, a consumer connecting later sees the last known status immediately, and the
// birth message on reconnect clears a stale "offline" that an earlier will left pinned.

#include <PubSubClient.h>

// Topics. Data topics reuse the bridge's topics/keys; status is deliberately off the subtree.
static const char* MQTT_TOPIC_TEMP    = "plantfloor/matrix/temperature";
static const char* MQTT_TOPIC_ACCEL   = "plantfloor/matrix/accelerometer";
static const char* MQTT_TOPIC_STATUS  = "plantfloor/status/matrix";
static const char* MQTT_STATUS_ONLINE  = "{\"online\":true}";
static const char* MQTT_STATUS_OFFLINE = "{\"online\":false}";

static WiFiClient   mqttWifi;
static PubSubClient mqttClient(mqttWifi);

static uint32_t mqttLastPublishMs        = 0;   // last SUCCESSFUL publish (0 = never); drives the age report
static uint32_t mqttLastAttemptMs        = 0;   // last publish ATTEMPT; gates the cadence (kept separate)
static uint32_t mqttLastConnectAttemptMs = 0;
static String   mqttServerActive         = "";   // "host:port" set on the client; "" = none
static bool     mqttBadHost              = false; // true when mqttHost is not a numeric IP

// UTC ISO8601 timestamp like "2026-07-14T21:03:00Z". Returns false when the system clock
// is not real yet (NTP has not synced), so the caller SKIPS publishing rather than stamp
// a 1970 time into the historian's time series. getLocalTime() only reports success once
// the clock year is plausible, which is exactly the "is time real" test we want.
static bool mqttIsoNow(char* buf, size_t n) {
  struct tm probe;
  if (!getLocalTime(&probe, 0)) return false;   // clock not set: caller must skip
  time_t now = time(nullptr);
  struct tm utc;
  gmtime_r(&now, &utc);                          // ts is UTC ('Z'), independent of clock TZ
  strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &utc);
  return true;
}

// Read-only accessors for /api/status, so a misconfigured broker is diagnosable without the
// Serial Monitor (surface degraded state, do not fail silently). "connected" alone is not
// enough: it stays true for ~45s after a broker loss, and says nothing about WHY a connect
// failed or whether data is actually flowing. So we also expose the raw state code, the
// bad-host flag, and the age of the last successful publish (the real "is data flowing" signal).
bool mqttIsConnected()      { return mqttClient.connected(); }
int  mqttState()            { return mqttClient.state(); }   // PubSubClient state code; 0 = connected
bool mqttHostInvalid()      { return mqttBadHost; }
long mqttSecsSincePublish() { return mqttLastPublishMs == 0 ? -1 : (long)((millis() - mqttLastPublishMs) / 1000); }

// Intentional teardown (user disabled MQTT, or changed broker). Publish a retained "offline"
// BEFORE the clean disconnect: a clean MQTT DISCONNECT tells the broker to DISCARD the will,
// so without this the retained "online" birth message would stay pinned forever and every
// subscriber would believe a deliberately-stopped board is still live. This makes a graceful
// shutdown set the same status the will sets on a crash.
static void mqttGoOffline() {
  if (mqttClient.connected()) {
    mqttClient.publish(MQTT_TOPIC_STATUS, MQTT_STATUS_OFFLINE, true);
    mqttClient.disconnect();
  }
}

// Called from settings.ino after every /api/settings merge. Only disturb the connection
// when the broker target or the on/off state actually changed, so saving an unrelated
// setting (idle, brightness) does not flap MQTT and re-announce online. On a real change,
// drop the connection and forget the active server; the next mqttTick() re-points and
// reconnects (or stays off if now disabled).
void mqttApplySettings() {
  String want = (settings.mqttOn && settings.mqttHost.length())
                ? settings.mqttHost + ":" + String(settings.mqttPort) : "";
  if (want != mqttServerActive) {
    mqttGoOffline();         // announce offline + clean disconnect if we were connected
    mqttServerActive = "";   // force mqttConnect() to re-point on the next tick
  }
}

// One connect attempt. PubSubClient's connect() BLOCKS loop() during the TCP connect and the
// CONNACK wait, so a misconfigured broker could freeze the animations. Two guards prevent that:
//   1. Require a numeric IP (IPAddress::fromString) and call setServer(IPAddress). A hostname
//      would trigger a BLOCKING DNS lookup inside connect(), the worst stall; the IP form skips
//      DNS entirely. The settings UI asks for a LAN IP for exactly this reason.
//   2. setSocketTimeout(4) bounds the CONNACK read, and the 5s backoff in mqttTick limits how
//      OFTEN we attempt. The residual is a TCP connect to a valid-but-unreachable IP, which the
//      LAN typically fails in ~1-3s: one worst-case stall of that order, then the backoff holds.
// ponytail: good enough at this cadence; a non-blocking connect is the upgrade if it ever matters.
static void mqttConnect() {
  IPAddress ip;
  if (!ip.fromString(settings.mqttHost)) {
    mqttBadHost = true;   // not a numeric IP: refuse rather than do a blocking DNS lookup
    return;               // surfaced in /api/status as mqtt_bad_host
  }
  mqttBadHost = false;
  String want = settings.mqttHost + ":" + String(settings.mqttPort);
  if (want != mqttServerActive) {
    mqttClient.setServer(ip, settings.mqttPort);   // IP form: no blocking DNS lookup
    mqttClient.setBufferSize(256);  // headroom over our ~100-byte payloads (some builds default to 128)
    mqttServerActive = want;
  }
  mqttClient.setSocketTimeout(4);   // bound the CONNACK wait
  mqttClient.setKeepAlive(30);      // will fires after ~1.5x this of silence

  // A stable client id from the MAC: the broker sees one identity across reconnects.
  String clientId = "esp32matrix-" + WiFi.macAddress();

  // Register the will FIRST (it can only be left at connect time), then, on success,
  // announce "online" to clear any retained "offline" a previous will left behind.
  bool ok = mqttClient.connect(
      clientId.c_str(),
      nullptr, nullptr,          // no auth: this is a trusted LAN broker
      MQTT_TOPIC_STATUS,         // will topic (off the plantfloor/matrix/# subtree)
      0,                         // will QoS 0
      true,                      // will retained
      MQTT_STATUS_OFFLINE);      // the dead-man's-switch payload
  if (ok) {
    mqttClient.publish(MQTT_TOPIC_STATUS, MQTT_STATUS_ONLINE, true);   // birth, retained
    Serial.println("[mqtt] connected to " + mqttServerActive);
  } else {
    Serial.printf("[mqtt] connect to %s failed (state=%d), will retry\n",
                  mqttServerActive.c_str(), mqttClient.state());
  }
}

// Build and send both readings. Retained + QoS 0, matching the bridge's field shape (see the
// header note on formatting: same keys/types, board-formatted bytes).
static void mqttPublishReadings() {
  char ts[24];
  if (!mqttIsoNow(ts, sizeof(ts))) return;   // NTP not ready: skip, do not stamp 1970

  // Chip temperature. %.1f matches the board's own HTTP formatting (String(tempC, 1)); it is a
  // bare JSON number, which is what the historian stores and OPC UA parses back to a Double.
  float tempC = temperatureRead();
  char tbuf[96];
  snprintf(tbuf, sizeof(tbuf), "{\"celsius\":%.1f,\"ts\":\"%s\"}", tempC, ts);
  bool ok = mqttClient.publish(MQTT_TOPIC_TEMP, tbuf, true);

  // Accelerometer, only when the IMU came up (matches the HTTP handler's imuReady guard).
  if (imuReady) {
    float ax, ay, az;
    readAccel(ax, ay, az);
    char abuf[128];
    snprintf(abuf, sizeof(abuf),
             "{\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"ts\":\"%s\"}", ax, ay, az, ts);
    mqttClient.publish(MQTT_TOPIC_ACCEL, abuf, true);
  }

  // Stamp the last SUCCESSFUL publish only when the send actually went out. This is what
  // mqtt_secs_since_publish reports, so it honestly means "data is flowing": it stays -1 while
  // NTP is unsynced (we returned above without stamping) and stops advancing if the broker drops.
  if (ok) mqttLastPublishMs = millis();
}

// Called every loop() iteration. Cheap no-op when MQTT is off or unconfigured.
void mqttTick() {
  // Off by default / no broker set: behave exactly as before. Announce offline + disconnect.
  if (!settings.mqttOn || settings.mqttHost.length() == 0) {
    mqttGoOffline();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) return;   // nothing to publish over yet

  // Seed NTP so time(nullptr) becomes real even if the clock app never runs, but ONLY if no
  // other feature has already started it (ntpStarted, set by the clock/calendar/tz handlers).
  // A blind configTime(0,0,...) here would restart SNTP and force the global timezone to UTC,
  // clobbering a running clock/calendar. We only need a valid UTC epoch; gmtime gives us that
  // regardless of the active TZ, so we never need to touch the timezone ourselves.
  if (!ntpStarted) {
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    ntpStarted = true;
  }

  if (!mqttClient.connected()) {
    if (millis() - mqttLastConnectAttemptMs < 5000) return;   // backoff so a bad host can't spin
    mqttLastConnectAttemptMs = millis();
    mqttConnect();
    return;
  }

  mqttClient.loop();   // service keepalive pings and the connection

  // Cadence gates on the last ATTEMPT, not the last successful publish. If it gated on success,
  // a not-yet-synced clock (mqttPublishReadings returns without stamping) would make this fire
  // every loop iteration, and mqttIsoNow's getLocalTime probe would stall the loop each time.
  uint32_t everyMs = settings.mqttEveryS * 1000UL;
  if (millis() - mqttLastAttemptMs >= everyMs) {
    mqttLastAttemptMs = millis();
    mqttPublishReadings();   // stamps mqttLastPublishMs itself, only on a real send
  }
}
