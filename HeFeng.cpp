
#include "HeFeng.h"

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>

#include "HttpsGetUtils.h"

HeFeng::HeFeng() {}

void HeFeng::fans(HeFengCurrentData *data, String id) {  // 获取粉丝数
  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  HTTPClient https;
  String url = "https://api.bilibili.com/x/relation/stat?vmid=" + id;
  Serial.print("[HTTPS] begin...bilibili\n");
  if (https.begin(*client, url)) {  // HTTPS
    // start connection and send HTTP header
    int httpCode = https.GET();
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been
      // handled
      Serial.printf("[HTTPS] GET... code: %d\n", https.errorToString(httpCode).c_str());

      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = https.getString();
        Serial.printf("[HTTPS] fans payload:%s\n", payload.c_str());
        DynamicJsonDocument jsonBuffer(2048);
        deserializeJson(jsonBuffer, payload);
        JsonObject root = jsonBuffer.as<JsonObject>();

        String follower = root["data"]["follower"];
        data->follower = follower;
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n",
                    https.errorToString(httpCode).c_str());
      data->follower = "-1";
    }

    https.end();
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
    data->follower = "-1";
  }
}

void HeFeng::doUpdateCurr(HeFengCurrentData *data, String key,
                          String location) {  // 获取天气

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  String url = "https://devapi.qweather.com/v7/weather/now?lang=en&location=" +
               location + "&key=" + key;
  Serial.printf("[HTTPS] doUpdateCurr begin...now url:%s\n", url.c_str());
  uint8_t *outbuf = NULL;
  size_t len = 0;
  bool result = HttpsGetUtils::getString(url.c_str(), outbuf, len);
  Serial.printf("result=%d, len=%d", result, len);
  if (outbuf && len) {
    Serial.printf("write to serial, buf=%x, len=%d\n", outbuf, len);
    // Serial.write(outbuf, len);
    Serial.println("parse json");
    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, (char *)outbuf, len);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    String httpCode = doc["code"].as<String>();  // "200"

    Serial.println("doUpdateCurr httpCode:" + httpCode);

    // httpCode will be negative on error
    if (httpCode == "200") {
      String tmp = doc["now"]["temp"];
      data->tmp = tmp;
      String fl = doc["now"]["feelsLike"];
      data->fl = fl;
      String hum = doc["now"]["humidity"];
      data->hum = hum;
      String wind_sc = doc["now"]["windScale"];
      data->wind_sc = wind_sc;
      String cond_code = doc["now"]["icon"];
      String meteoconIcon = getMeteoconIcon(cond_code);
      String cond_txt = doc["now"]["text"];
      data->cond_txt = cond_txt;
      data->iconMeteoCon = meteoconIcon;
      Serial.printf("[HTTPS] doUpdateCurr tmp:%s, fl:%s, hum:%s, wind_sc:%s\n",
                    tmp.c_str(), fl.c_str(), hum.c_str(), wind_sc.c_str());

    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n",
                    httpCode.c_str());
      data->tmp = "-1";
      data->fl = "-1";
      data->hum = "-1";
      data->wind_sc = "-1";
      data->cond_txt = "no network";
      data->iconMeteoCon = ")";
    }
  } else {
    Serial.printf("[HTTPS] Unable to connect\n");
    data->tmp = "-1";
    data->fl = "-1";
    data->hum = "-1";
    data->wind_sc = "-1";
    data->cond_txt = "no network";
    data->iconMeteoCon = ")";
  }
}

void HeFeng::doUpdateFore(HeFengForeData *data, String key,
                          String location) {  // 获取预报

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  String url =
      "https://devapi.qweather.com/v7/weather/3d?lang=en&location=" + location +
      "&key=" + key;
  Serial.printf("[HTTPS] begin...forecast url:%s\n", url.c_str());
  uint8_t *outbuf = NULL;
  size_t len = 0;
  bool result = HttpsGetUtils::getString(url.c_str(), outbuf, len);
  Serial.printf("result=%d, len=%d", result, len);
  if (outbuf && len) {
    Serial.printf("write to serial, buf=%x, len=%d\n", outbuf, len);
    // Serial.write(outbuf, len);
    Serial.println("doUpdateFore parse json");
    DynamicJsonDocument doc(3072);
    DeserializationError error = deserializeJson(doc, (char *)outbuf, len);
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }
    String httpCode = doc["code"].as<String>();  // "200"

    Serial.println("doUpdateFore httpCode:" + httpCode);

    // httpCode will be negative on error
    if (httpCode == "200") {
      JsonObject root = doc.as<JsonObject>();
      int i;
      for (i = 0; i < 3; i++) {
        String tmp_min = root["daily"][i]["tempMin"];
        data[i].tmp_min = tmp_min;
        String tmp_max = root["daily"][i]["tempMax"];
        data[i].tmp_max = tmp_max;
        String datestr = root["daily"][i]["fxDate"];
        data[i].datestr = datestr.substring(5, datestr.length());
        String cond_code = root["daily"][i]["iconDay"];
        String meteoconIcon = getMeteoconIcon(cond_code);
        data[i].iconMeteoCon = meteoconIcon;
        Serial.printf(
            "[HTTPS] doUpdateCurr tmp_min:%s, "
            "tmp_max:%s, datestr:%s, cond_code:%s\n",
            tmp_min.c_str(), tmp_max.c_str(), datestr.c_str(),
            cond_code.c_str());
      }
    } else {
      Serial.printf("[HTTPS] GET... failed, error: %s\n", httpCode);
      int i;
      for (i = 0; i < 3; i++) {
        data[i].tmp_min = "-1";
        data[i].tmp_max = "-1";
        data[i].datestr = "N/A";
        data[i].iconMeteoCon = ")";
      }
    }
  } else {
    Serial.printf("[HTTPS] doUpdateFore Unable to connect\n");
    int i;
    for (i = 0; i < 3; i++) {
      data[i].tmp_min = "-1";
      data[i].tmp_max = "-1";
      data[i].datestr = "N/A";
      data[i].iconMeteoCon = ")";
    }
  }
}

String HeFeng::getMeteoconIcon(String cond_code) {  // 获取天气图标
  if (cond_code == "100" || cond_code == "9006") {
    return "B";
  }
  if (cond_code == "999") {
    return ")";
  }
  if (cond_code == "104") {
    return "D";
  }
  if (cond_code == "500") {
    return "E";
  }
  if (cond_code == "503" || cond_code == "504" || cond_code == "507" ||
      cond_code == "508") {
    return "F";
  }
  if (cond_code == "499" || cond_code == "901") {
    return "G";
  }
  if (cond_code == "103") {
    return "H";
  }
  if (cond_code == "502" || cond_code == "511" || cond_code == "512" ||
      cond_code == "513") {
    return "L";
  }
  if (cond_code == "501" || cond_code == "509" || cond_code == "510" ||
      cond_code == "514" || cond_code == "515") {
    return "M";
  }
  if (cond_code == "102") {
    return "N";
  }
  if (cond_code == "213") {
    return "O";
  }
  if (cond_code == "302" || cond_code == "303") {
    return "P";
  }
  if (cond_code == "305" || cond_code == "308" || cond_code == "309" ||
      cond_code == "314" || cond_code == "399") {
    return "Q";
  }
  if (cond_code == "306" || cond_code == "307" || cond_code == "310" ||
      cond_code == "311" || cond_code == "312" || cond_code == "315" ||
      cond_code == "316" || cond_code == "317" || cond_code == "318") {
    return "R";
  }
  if (cond_code == "200" || cond_code == "201" || cond_code == "202" ||
      cond_code == "203" || cond_code == "204" || cond_code == "205" ||
      cond_code == "206" || cond_code == "207" || cond_code == "208" ||
      cond_code == "209" || cond_code == "210" || cond_code == "211" ||
      cond_code == "212") {
    return "S";
  }
  if (cond_code == "300" || cond_code == "301") {
    return "T";
  }
  if (cond_code == "400" || cond_code == "408") {
    return "U";
  }
  if (cond_code == "407") {
    return "V";
  }
  if (cond_code == "401" || cond_code == "402" || cond_code == "403" ||
      cond_code == "409" || cond_code == "410") {
    return "W";
  }
  if (cond_code == "304" || cond_code == "313" || cond_code == "404" ||
      cond_code == "405" || cond_code == "406") {
    return "X";
  }
  if (cond_code == "101") {
    return "Y";
  }
  return ")";
}
