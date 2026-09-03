#include <WiFi.h>
#include <WebServer.h>

// Set your own local access-point credentials before flashing.
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

const int MQ135_PIN = 34;

// Voltaj bölücü:
// AO -> 20k -> ADC -> 10k -> GND
const float R_TOP = 20000.0;
const float R_BOTTOM = 10000.0;

// MQ-135 modülündeki yük direnci.
// Birçok modülde yaklaşık 10kΩ bulunur.
// Kesin değer modüle göre değişebilir.
const float RL = 10000.0;

// Temiz havada kalibre edilecek R0
float R0 = 0;

// ADC ölçümünden MQ-135 gerçek AO voltajını hesapla
float getSensorVoltage(int raw) {
  float adcVoltage = raw * (3.3 / 4095.0);

  return adcVoltage * ((R_TOP + R_BOTTOM) / R_BOTTOM);
}

// Sensör direnci Rs
float calculateRs(float sensorVoltage) {
  if (sensorVoltage <= 0.01)
    return 9999999;

  // Sensör/modül 5V ile çalışıyor
  return RL * ((5.0 / sensorVoltage) - 1.0);
}

// İlk açılışta temiz hava referansı oluştur
void calibrate() {
  Serial.println("Kalibrasyon basladi...");

  float totalRs = 0;
  int validSamples = 0;

  for (int i = 0; i < 100; i++) {
    int raw = analogRead(MQ135_PIN);
    float voltage = getSensorVoltage(raw);

    if (voltage > 0.01 && voltage < 4.9) {
      float rs = calculateRs(voltage);
      totalRs += rs;
      validSamples++;
    }

    delay(100);
  }

  if (validSamples > 0) {
    float cleanAirRs = totalRs / validSamples;

    /*
      MQ-135 datasheet eğrisinde temiz hava Rs/R0
      yaklaşık 3.6 civarı kabul edilir.
    */
    R0 = cleanAirRs / 3.6;
  }

  Serial.print("R0: ");
  Serial.println(R0);
}

String getQuality(float ratio) {
  // Bunlar kesin AQI değerleri değildir.
  // Göreli sınıflandırmadır.

  if (ratio > 3.2)
    return "Iyi";
  else if (ratio > 2.5)
    return "Normal";
  else if (ratio > 1.8)
    return "Orta";
  else if (ratio > 1.2)
    return "Kotu";
  else
    return "Cok Kotu";
}

void handleRoot() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="tr">

<head>

<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">

<title>MQ-135 Hava Kalitesi</title>

<style>

body {
  background: #111;
  color: white;
  font-family: Arial;
  text-align: center;
  margin: 0;
  padding: 30px;
}

.card {
  max-width: 400px;
  margin: auto;
  background: #222;
  padding: 25px;
  border-radius: 18px;
}

.value {
  font-size: 32px;
  font-weight: bold;
  margin-bottom: 20px;
}

.small {
  color: #aaa;
  font-size: 14px;
}

</style>

<script>

function update() {

  fetch("/data")
  .then(r => r.json())
  .then(d => {

    document.getElementById("adc").innerText = d.adc;

    document.getElementById("adcVoltage").innerText =
      d.adcVoltage.toFixed(3) + " V";

    document.getElementById("sensorVoltage").innerText =
      d.sensorVoltage.toFixed(3) + " V";

    document.getElementById("rs").innerText =
      (d.rs / 1000).toFixed(1) + " kΩ";

    document.getElementById("ratio").innerText =
      d.ratio.toFixed(2);

    document.getElementById("quality").innerText =
      d.quality;
  });

}

setInterval(update, 1000);

window.onload = update;

</script>

</head>

<body>

<div class="card">

<h1>MQ-135</h1>

<p>Hava Kalitesi</p>
<div id="quality" class="value">---</div>

<p>ADC</p>
<div id="adc" class="value">---</div>

<p>ESP32 ADC Voltajı</p>
<div id="adcVoltage" class="value">---</div>

<p>MQ-135 Gerçek AO Voltajı</p>
<div id="sensorVoltage" class="value">---</div>

<p>Rs</p>
<div id="rs" class="value">---</div>

<p>Rs / R0</p>
<div id="ratio" class="value">---</div>

<p class="small">
Bu değer gerçek AQI değildir.<br>
MQ-135 ile göreli gaz/hava değişimi gösterilir.
</p>

</div>

</body>

</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleData() {

  int raw = analogRead(MQ135_PIN);

  float adcVoltage =
      raw * (3.3 / 4095.0);

  float sensorVoltage =
      getSensorVoltage(raw);

  float rs =
      calculateRs(sensorVoltage);

  float ratio = 0;

  if (R0 > 0)
    ratio = rs / R0;

  String quality =
      getQuality(ratio);

  String json = "{";

  json += "\"adc\":";
  json += String(raw);
  json += ",";

  json += "\"adcVoltage\":";
  json += String(adcVoltage, 4);
  json += ",";

  json += "\"sensorVoltage\":";
  json += String(sensorVoltage, 4);
  json += ",";

  json += "\"rs\":";
  json += String(rs, 2);
  json += ",";

  json += "\"ratio\":";
  json += String(ratio, 3);
  json += ",";

  json += "\"quality\":\"";
  json += quality;
  json += "\"";

  json += "}";

  server.send(200, "application/json", json);
}

void setup() {

  Serial.begin(115200);

  analogReadResolution(12);

  Serial.println("MQ-135 isinmasi bekleniyor...");

  // Basit örnek için 30 saniye.
  // Daha iyi kalibrasyon için sensörü önceden
  // uzun süre açık bırakmak daha doğru.
  delay(30000);

  calibrate();

  WiFi.softAP(ssid, password);

  Serial.print("Adres: http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/data", handleData);

  server.begin();
}

void loop() {
  server.handleClient();
}
