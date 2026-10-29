#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "backgrounds.h"
#include "custom_remote.h"

// Local-only configuration portal. Wi-Fi is supported; Bluetooth is a
// future feature and is deliberately NOT started by this module.
namespace WifiPortal {
static WebServer server(80);
static Preferences prefs;
static String apSsid, apPass, staSsid, staPass;
static bool staEnabled = false;
static bool apEnabled = true;
static bool btRequested = false;
static uint32_t lastRetry = 0;
static bool ready = false;

static String escapeHtml(const String &s) {
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    switch (s[i]) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += s[i]; break;
    }
  }
  return out;
}
static bool authorized() {
  if (server.authenticate("admin", apPass.c_str())) return true;
  server.requestAuthentication();
  return false;
}
static String statusText() {
  if (!staEnabled || staSsid.isEmpty()) return "Not configured";
  if (WiFi.status() == WL_CONNECTED) return "Connected: " + WiFi.localIP().toString();
  return "Connecting / disconnected";
}
static void applyWifi() {
  // Do not allow disabling both interfaces: preserve a setup recovery path.
  if (!apEnabled && !staEnabled) apEnabled = true;
  WiFi.mode(apEnabled ? (staEnabled ? WIFI_AP_STA : WIFI_AP) : WIFI_STA);
  if (apEnabled) WiFi.softAP(apSsid.c_str(), apPass.c_str());
}
static void startSta() {
  if (!staEnabled) { WiFi.disconnect(false, false); return; }
  WiFi.disconnect(false, false);
  if (staEnabled && !staSsid.isEmpty()) {
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    lastRetry = millis();
  }
}
static void startAp() {
  applyWifi();
}
static void setStationEnabled(bool enabled) {
  staEnabled = enabled && !staSsid.isEmpty();
  prefs.putBool("sta_on", staEnabled);
  applyWifi();
  startSta();
}
static void setApEnabled(bool enabled) {
  apEnabled = enabled;
  if (!apEnabled && !staEnabled) apEnabled = true;
  prefs.putBool("ap_on", apEnabled);
  applyWifi();
}
static void setBluetoothRequested(bool enabled) {
  // This is a persisted preference, not a BLE controller start.
  btRequested = enabled;
  prefs.putBool("ble_on", enabled);
}
static void redirectHome() {
  server.sendHeader("Location", "/", true);
  server.send(303, "text/plain", "Saved");
}
static void home() {
  if (!authorized()) return;
  String html = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Waveshare Remote Setup</title><style>body{font:16px system-ui;background:#111827;color:#e5e7eb;max-width:650px;margin:24px auto;padding:0 16px}
section{background:#1f2937;padding:18px;border-radius:12px;margin:15px 0}input,select,button{box-sizing:border-box;width:100%;padding:11px;margin:7px 0 13px;border-radius:7px;border:1px solid #4b5563;background:#374151;color:white}
button{background:#2563eb;border:0;font-weight:bold}label{display:block}small{color:#9ca3af}h1{font-size:24px}</style></head><body>
<h1>ESP32-S3 Remote Settings</h1><section><h2>Network status</h2>)HTML";
  html += "<p>AP: <b>" + escapeHtml(apSsid) + "</b> | " + WiFi.softAPIP().toString() + "</p>";
  html += "<p>Home Wi-Fi: <b>" + escapeHtml(statusText()) + "</b></p>";
  html += "<p>AP: " + String(apEnabled ? "enabled" : "disabled") + " | BLE preference: " + String(btRequested ? "enabled (not active)" : "disabled") + "</p>";
  html += "<p>Wi-Fi MAC: " + WiFi.macAddress() + "</p></section>";
  html += R"HTML(<section><h2>Home Wi-Fi</h2><form method="post" action="/wifi">
<label>Network name (SSID)</label><input name="ssid" maxlength="32" value=")HTML";
  html += escapeHtml(staSsid);
  html += R"HTML("><label>Password (leave blank to keep saved password)</label><input type="password" name="password" maxlength="63">
<label><input style="width:auto" type="checkbox" name="enabled" value="1" )HTML";
  if (staEnabled) html += "checked";
  html += R"HTML(> Connect to home Wi-Fi</label><button>Save Wi-Fi settings</button></form>
<p><a style="color:#93c5fd" href="/scan">Scan nearby Wi-Fi networks</a></p></section>
<section><h2>Setup hotspot</h2><form method="post" action="/ap">
<label>Hotspot SSID</label><input name="ssid" maxlength="32" value=")HTML";
  html += escapeHtml(apSsid);
  html += R"HTML("><label>New hotspot / web admin password (8–63 characters)</label>
<input type="password" name="password" minlength="8" maxlength="63" required>
<small>Changing the hotspot password also changes the web login password (username: admin). You will need to reconnect.</small>
<button>Update hotspot</button></form></section>
<section><h2>Custom remote</h2><p><a style="color:#93c5fd" href="/custom">Edit custom buttons and layout</a></p></section><section><h2>Photos</h2><p><a style="color:#93c5fd" href="/backgrounds">Manage room backgrounds</a></p></section><section><h2>Bluetooth</h2><p>BLE pairing and discovery are not implemented yet.</p>
<small>Bluetooth will be configured here after we add and test its firmware separately. This page does not activate a Bluetooth radio.</small></section>
<small>Local HTTP only. Do not expose port 80 to the internet.</small></body></html>)HTML";
  server.send(200, "text/html; charset=utf-8", html);
}
static void scan() {
  if (!authorized()) return;
  String html = "<html><meta name='viewport' content='width=device-width,initial-scale=1'><body><h2>Nearby networks</h2>";
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) html += "<p>Scanning... refresh this page in a few seconds.</p>";
  else if (n < 0) { WiFi.scanNetworks(true, true); html += "<p>Scan started. Refresh in a few seconds.</p>"; }
  else {
    for (int i = 0; i < n; ++i) {
      html += "<p>" + escapeHtml(WiFi.SSID(i)) + " (" + String(WiFi.RSSI(i)) + " dBm)</p>";
    }
    WiFi.scanDelete();
  }
  html += "<p><a href='/'>Back to settings</a></p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}
static void saveWifi() {
  if (!authorized()) return;
  String ssid = server.arg("ssid");
  if (ssid.length() > 32) { server.send(400, "text/plain", "Invalid SSID"); return; }
  staSsid = ssid;
  if (server.arg("password").length()) staPass = server.arg("password");
  if (staPass.length() > 63) { server.send(400, "text/plain", "Invalid password"); return; }
  staEnabled = server.hasArg("enabled") && !staSsid.isEmpty();
  prefs.putString("sta_ssid", staSsid);
  prefs.putString("sta_pass", staPass);
  prefs.putBool("sta_on", staEnabled);
  applyWifi();
  startSta();
  redirectHome();
}
static void saveAp() {
  if (!authorized()) return;
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  if (ssid.isEmpty() || ssid.length() > 32 || pass.length() < 8 || pass.length() > 63) {
    server.send(400, "text/plain", "SSID or password invalid"); return;
  }
  prefs.putString("ap_ssid", ssid);
  prefs.putString("ap_pass", pass);
  server.send(200, "text/plain", "Saved. Reboot the ESP32 to apply the new AP name/password.");
}
static void begin() {
  prefs.begin("remote-net", false);
  apSsid = prefs.getString("ap_ssid", "Waveshare-Remote");
  apPass = prefs.getString("ap_pass", "SetupRemote42!");
  staSsid = prefs.getString("sta_ssid", "");
  staPass = prefs.getString("sta_pass", "");
  staEnabled = prefs.getBool("sta_on", false);
  apEnabled = prefs.getBool("ap_on", true);
  btRequested = prefs.getBool("ble_on", false);
  if (!apEnabled && !staEnabled) apEnabled = true;
  startAp();
  startSta();
  Backgrounds::begin();
  Backgrounds::installRoutes(server, authorized);
  CustomRemote::installRoutes(server, authorized);
  server.on("/", HTTP_GET, home);
  server.on("/scan", HTTP_GET, scan);
  server.on("/wifi", HTTP_POST, saveWifi);
  server.on("/ap", HTTP_POST, saveAp);
  server.onNotFound([]() { if (authorized()) server.send(404, "text/plain", "Not found"); });
  server.begin();
  ready = true;
  Serial.printf("[NET] AP %s IP %s; web login admin / AP password\n",
                apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}
static void loop() {
  if (!ready) return;
  server.handleClient();
  if (staEnabled && !staSsid.isEmpty() && WiFi.status() != WL_CONNECTED &&
      millis() - lastRetry > 20000) startSta();
}
} // namespace WifiPortal
