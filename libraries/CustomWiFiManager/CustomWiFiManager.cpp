/**************************************************************
   WiFiManager is a library for the ESP8266/Arduino platform
   (https://github.com/esp8266/Arduino) to enable easy
   configuration and reconfiguration of WiFi credentials using a Captive Portal
   inspired by:
   http://www.esp8266.com/viewtopic.php?f=29&t=2520
   https://github.com/chriscook8/esp-arduino-apboot
   https://github.com/esp8266/Arduino/tree/master/libraries/DNSServer/examples/CaptivePortalAdvanced
   Built by AlexT https://github.com/tzapu
   Licensed under MIT license
 **************************************************************/

#include "CustomWiFiManager.h"

WiFiManagerParameter::WiFiManagerParameter(const char *custom) {
  _id = NULL;
  _placeholder = NULL;
  _length = 0;
  _value = NULL;

  _customHTML = custom;
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length) {
  init(id, placeholder, defaultValue, length, "");
}

WiFiManagerParameter::WiFiManagerParameter(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  init(id, placeholder, defaultValue, length, custom);
}

void WiFiManagerParameter::init(const char *id, const char *placeholder, const char *defaultValue, int length, const char *custom) {
  _id = id;
  _placeholder = placeholder;
  _length = length;
  _value = new char[length + 1];
  for (int i = 0; i < length + 1; i++) {
    _value[i] = 0;
  }
  if (defaultValue != NULL) {
    strncpy(_value, defaultValue, length);
  }

  _customHTML = custom;
}

WiFiManagerParameter::~WiFiManagerParameter() {
  if (_value != NULL) {
    delete[] _value;
  }
}

const char* WiFiManagerParameter::getValue() {
  return _value;
}
const char* WiFiManagerParameter::getID() {
  return _id;
}
const char* WiFiManagerParameter::getPlaceholder() {
  return _placeholder;
}
int WiFiManagerParameter::getValueLength() {
  return _length;
}
const char* WiFiManagerParameter::getCustomHTML() {
  return _customHTML;
}


WiFiManager::WiFiManager() {
    _max_params = WIFI_MANAGER_MAX_PARAMS;
    _params = (WiFiManagerParameter**)malloc(_max_params * sizeof(WiFiManagerParameter*));
}

WiFiManager::~WiFiManager()
{
    if (_params != NULL)
    {
        DEBUG_WM(F("freeing allocated params!"));
        free(_params);
    }
}

bool WiFiManager::addParameter(WiFiManagerParameter *p) {
  if(_paramsCount + 1 > _max_params)
  {
    // rezise the params array
    _max_params += WIFI_MANAGER_MAX_PARAMS;
    DEBUG_WM(F("Increasing _max_params to:"));
    DEBUG_WM(_max_params);
    WiFiManagerParameter** new_params = (WiFiManagerParameter**)realloc(_params, _max_params * sizeof(WiFiManagerParameter*));
    if (new_params != NULL) {
      _params = new_params;
    } else {
      DEBUG_WM(F("ERROR: failed to realloc params, size not increased!"));
      return false;
    }
  }

  _params[_paramsCount] = p;
  _paramsCount++;
  DEBUG_WM(F("Adding parameter"));
  DEBUG_WM(p->getID());
  DEBUG_WM(p->getPlaceholder());
  DEBUG_WM(p->getValue());
  return true;
}

/**
 * 功能描述：配置web服务以及dns重定向功能
 */
void WiFiManager::setupConfigPortal() {
  // 配置dns服务（这里主要是用到认证功能）
  dnsServer.reset(new DNSServer());
  // 配置web服务 主要用来显示web页面 实现AP配网之类的功能
  server.reset(new ESP8266WebServer(80));

  DEBUG_WM(F(""));
  _configPortalStart = millis();

  DEBUG_WM(F("Configuring access point... "));
  DEBUG_WM(_apName);
  if (_apPassword != NULL) {
    if (strlen(_apPassword) < 8 || strlen(_apPassword) > 63) {
      // fail passphrase to short or long!
      DEBUG_WM(F("Invalid AccessPoint password. Ignoring"));
      _apPassword = NULL;
    }
    DEBUG_WM(_apPassword);
  }

  //optional soft ip config
  // 配置自定义固定AP信息
  if (_ap_static_ip) {
    DEBUG_WM(F("Custom AP IP/GW/Subnet"));
    WiFi.softAPConfig(_ap_static_ip, _ap_static_gw, _ap_static_sn);
  }

  // 配置AP热点
  if (_apPassword != NULL) {
    WiFi.softAP(_apName, _apPassword);//password option
  } else {
    WiFi.softAP(_apName);
  }

  delay(500); // Without delay I've seen the IP address blank
  DEBUG_WM(F("AP IP address: "));
  DEBUG_WM(WiFi.softAPIP());

  /* Setup the DNS server redirecting all the domains to the apIP */
  // 配置DNS 重定向功能 全部请求都重定向到AP
  dnsServer->setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer->start(DNS_PORT, "*", WiFi.softAPIP());

  /* Setup web pages: root, wifi config pages, SO captive portal detectors and not found. */
  // 配置Web服务器的功能 这里就是实现AP配网功能
  server->on(String(F("/")).c_str(), std::bind(&WiFiManager::handleRoot, this));
  server->on(String(F("/wifi")).c_str(), std::bind(&WiFiManager::handleWifi, this, true));
  server->on(String(F("/0wifi")).c_str(), std::bind(&WiFiManager::handleWifi, this, false));
  server->on(String(F("/wifisave")).c_str(), std::bind(&WiFiManager::handleWifiSave, this));
  server->on(String(F("/i")).c_str(), std::bind(&WiFiManager::handleInfo, this));
  server->on(String(F("/r")).c_str(), std::bind(&WiFiManager::handleReset, this));
  //server->on("/generate_204", std::bind(&WiFiManager::handle204, this));  //Android/Chrome OS captive portal check.
  server->on(String(F("/fwlink")).c_str(), std::bind(&WiFiManager::handleRoot, this));  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server->onNotFound (std::bind(&WiFiManager::handleNotFound, this));
  server->begin(); // Web server start
  DEBUG_WM(F("HTTP server started"));
}

/**
 * 功能说明：自动连接到上一次保存的AP热点，如果连接失败，进入AP配网模式
 */
boolean WiFiManager::autoConnect() {
  String ssid = "ESP" + String(ESP.getChipId());
  return autoConnect(ssid.c_str(), NULL);
}

/**
 * 功能说明：自动连接到上一次保存的AP热点，如果连接失败，进入AP配网模式
 * @param apName ap模式下的名字
 * @param apPassword ap模式下的密码
 */
boolean WiFiManager::autoConnect(char const *apName, char const *apPassword) {
  DEBUG_WM(F(""));
  DEBUG_WM(F("AutoConnect"));

  // read eeprom for ssid and pass  这里可以从eeprom获取ssid和password
  String ssid = getSSID();
  String pass = getPassword();

  // attempt to connect; should it fail, fall back to AP
  WiFi.mode(WIFI_STA);

  if (connectWifi(ssid, pass) == WL_CONNECTED)   {
    DEBUG_WM(F("IP Address:"));
    DEBUG_WM(WiFi.localIP());
    //connected
    return true;
  }
  // 如果连接失败 直接进入配网管理
  return startConfigPortal(apName, apPassword);
}

/**
 * 功能描述：配置认证服务是否超时
 */
boolean WiFiManager::configPortalHasTimeout(){
    // 如果没有配置超时或者 ap模式下sta的个数超过0个 就直接返回false
    if(_configPortalTimeout == 0 || wifi_softap_get_station_num() > 0){
      // 重新配置开始时间
      _configPortalStart = millis(); // kludge, bump configportal start time to skew timeouts
      return false;
    }
    // 如果配置了超时 但是一直没有sta连接上了的话 就返回true
    return (millis() > _configPortalStart + _configPortalTimeout);
}

/**
 * 功能说明：配置认证，默认ssid为esp+chipid
 */
boolean WiFiManager::startConfigPortal() {
  String ssid = "ESP" + String(ESP.getChipId());
  return startConfigPortal(ssid.c_str(), NULL);
}

/**
 * 功能说明：配置认证
 */
boolean  WiFiManager::startConfigPortal(char const *apName, char const *apPassword) {

  // 如果没有连接上网络 切换到AP模式
  if(!WiFi.isConnected()){
    WiFi.persistent(false);
    // disconnect sta, start ap
    WiFi.disconnect(); //  this alone is not enough to stop the autoconnecter
    WiFi.mode(WIFI_AP);
    WiFi.persistent(true);
  } 
  else {
    //setup AP
    WiFi.mode(WIFI_AP_STA);
    DEBUG_WM(F("SET AP STA"));
  }


  _apName = apName;
  _apPassword = apPassword;

  //notify we entered AP mode
  if ( _apcallback != NULL) {
    _apcallback(this);
  }

  connect = false;
  // 配置信息 这是非常关键的一个方法
  setupConfigPortal();

  while(1){

    // check if timeout
    if(configPortalHasTimeout()) break;

    //DNS 处理dns请求
    dnsServer->processNextRequest();
    //HTTP 处理web请求
    server->handleClient();


    if (connect) {
      connect = false;
      delay(2000);
      DEBUG_WM(F("Connecting to new AP"));
      DEBUG_WM(_ssid);
      DEBUG_WM(_pass);
      // using user-provided  _ssid, _pass in place of system-stored ssid and pass
      // 使用用户提供的wifi账号密码来设置
      if (connectWifi(_ssid, _pass) != WL_CONNECTED) {
        DEBUG_WM(F("Failed to connect."));
      } else {
        //connected
        WiFi.mode(WIFI_STA);
        //notify that configuration has changed and any optional parameters should be saved
        // 通知外面wifi连接成功，可以做一些额外操作
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
      // 判断配置后是否断开,不管有没有连接上网络
      if (_shouldBreakAfterConfig) {
        //flag set to exit after config after trying to connect
        //notify that configuration has changed and any optional parameters should be saved
        if ( _savecallback != NULL) {
          //todo: check if any custom parameters actually exist, and check if they really changed maybe
          _savecallback();
        }
        break;
      }
    }
    yield();
  }

  server.reset();
  dnsServer.reset();

  return  WiFi.status() == WL_CONNECTED;
}

/**
 * 功能描述：连接Ap热点
 * @param ssid 热点名称
 * @param pass 热点密码
 * @return 连接状态
 */
int WiFiManager::connectWifi(String ssid, String pass) {
  DEBUG_WM(F("Connecting as wifi client..."));

  // check if we've got static_ip settings, if we do, use those.
  // 判断我们是否配置了sta（ip地址、网关地址、子网掩码），有配置就设置配置内容
  if (_sta_static_ip) {
    DEBUG_WM(F("Custom STA IP/GW/Subnet"));
    WiFi.config(_sta_static_ip, _sta_static_gw, _sta_static_sn);
    DEBUG_WM(WiFi.localIP());
  }
  //fix for auto connect racing issue
  // 如果连接成功了 直接返回
  if (WiFi.status() == WL_CONNECTED && (WiFi.SSID() == ssid)) {
    DEBUG_WM(F("Already connected. Bailing out."));
    return WL_CONNECTED;
  }
  //check if we have ssid and pass and force those, if not, try with last saved values
  // 判断有没有传入自定义的 ssid和 pass，没有就用上次存储的
  if (ssid != "") {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    if (WiFi.SSID() != "") {
      DEBUG_WM(F("Using last saved values, should be faster"));
      //trying to fix connection in progress hanging
      ETS_UART_INTR_DISABLE();
      wifi_station_disconnect();
      ETS_UART_INTR_ENABLE();

      WiFi.begin();
    } else {
      DEBUG_WM(F("No saved credentials"));
    }
  }

  // 等待连接结果
  int connRes = waitForConnectResult();
  DEBUG_WM ("Connection result: ");
  DEBUG_WM ( connRes );
  //not connected, WPS enabled, no pass - first attempt
  #ifdef NO_EXTRA_4K_HEAP
  if (_tryWPS && connRes != WL_CONNECTED && pass == "") {
    startWPS();
    //should be connected at the end of WPS
    connRes = waitForConnectResult();
  }
  #endif
  return connRes;
}

/**
 * 功能描述：等待连接结果
 * 如果没有设置连接超时 直接返回状态
 * 如果设置了连接超时 在规定时间内等待连接状态
 */
uint8_t WiFiManager::waitForConnectResult() {
  if (_connectTimeout == 0) {
    return WiFi.waitForConnectResult();
  } else {
    DEBUG_WM (F("Waiting for connection result with time out"));
    unsigned long start = millis();
    boolean keepConnecting = true;
    uint8_t status;
    while (keepConnecting) {
      status = WiFi.status();
      if (millis() > start + _connectTimeout) {
        keepConnecting = false;
        DEBUG_WM (F("Connection timed out"));
      }
      if (status == WL_CONNECTED) {
        keepConnecting = false;
      }
      delay(100);
    }
    return status;
  }
}

void WiFiManager::startWPS() {
  DEBUG_WM(F("START WPS"));
  WiFi.beginWPSConfig();
  DEBUG_WM(F("END WPS"));
}

String WiFiManager::getSSID() {
  if (_ssid == "") {
    DEBUG_WM(F("Reading SSID"));
    _ssid = WiFi.SSID();
    DEBUG_WM(F("SSID: "));
    DEBUG_WM(_ssid);
  }
  return _ssid;
}

String WiFiManager::getPassword() {
  if (_pass == "") {
    DEBUG_WM(F("Reading Password"));
    _pass = WiFi.psk();
    DEBUG_WM("Password: " + _pass);
    //DEBUG_WM(_pass);
  }
  return _pass;
}

/**
 * 功能描述：获取AP配置的SSID
 */
String WiFiManager::getConfigPortalSSID() {
  return _apName;
}

/**
 * 功能描述：重置配置
 */
void WiFiManager::resetSettings() {
  DEBUG_WM(F("settings invalidated"));
  DEBUG_WM(F("THIS MAY CAUSE AP NOT TO START UP PROPERLY. YOU NEED TO COMMENT IT OUT AFTER ERASING THE DATA."));
  WiFi.disconnect(true);// 断开wifi连接，设置当前配置SSID和pwd为null 设置为true，那么就会关闭Station模式
  //delay(200);
}

/**
 * 功能描述：配置认证超时
 * @param seconds 秒数为单位
 */
void WiFiManager::setTimeout(unsigned long seconds) {
  setConfigPortalTimeout(seconds);
}

/**
 * 功能描述：配置认证超时
 * @param seconds 秒数为单位
 */
void WiFiManager::setConfigPortalTimeout(unsigned long seconds) {
  _configPortalTimeout = seconds * 1000;
}

/**
 * 功能描述：设置sta连接超时
 * @param seconds 秒数为单位
 */
void WiFiManager::setConnectTimeout(unsigned long seconds) {
  _connectTimeout = seconds * 1000;
}

/**
 * 功能描述：设置是否打开debug
 * @param debug true or false
 */
void WiFiManager::setDebugOutput(boolean debug) {
  _debug = debug;
}

/**
 * 功能描述：设置固定AP，包括ip、网关、子网掩码
 * @param ip  ip地址
 * @param gw  网关地址
 * @param sn  子网掩码
 */
void WiFiManager::setAPStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _ap_static_ip = ip;
  _ap_static_gw = gw;
  _ap_static_sn = sn;
}

/**
 * 功能描述：设置固定STA，包括ip、网关、子网掩码
 * @param ip  ip地址
 * @param gw  网关地址
 * @param sn  子网掩码
 */
void WiFiManager::setSTAStaticIPConfig(IPAddress ip, IPAddress gw, IPAddress sn) {
  _sta_static_ip = ip;
  _sta_static_gw = gw;
  _sta_static_sn = sn;
}

void WiFiManager::setMinimumSignalQuality(int quality) {
  _minimumQuality = quality;
}

void WiFiManager::setBreakAfterConfig(boolean shouldBreak) {
  _shouldBreakAfterConfig = shouldBreak;
}

/** Handle root or redirect to captive portal */
/**
 * 功能描述：响应root请求，也就是ip请求
 */
void WiFiManager::handleRoot() {
  DEBUG_WM(F("Handle root"));
  if (captivePortal()) { // If caprive portal redirect instead of displaying the page.
    return;
  }
  // 这里就会显示一个通用页面了
  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", _pageTitle);
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page.replace("{bc}", _backgroundColor);
  page.replace("{tc}", _textColor);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEADER_END);
  if(_imgBase64 != NULL){
    page += FPSTR(HTTP_IMAGE);
    page.replace("{is}", _imgBase64);
  }

  page += String(F("<h3>永远相信美好的事情即将发生^_^</h3>"));
  page += FPSTR(HTTP_PORTAL_OPTIONS);
  page += FPSTR(HTTP_END);

  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);

}

/** Wifi config page handler */
/**
 * 功能描述：wifi配置页面
 */
void WiFiManager::handleWifi(boolean scan) {

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "配置 ESP");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page.replace("{bc}", _backgroundColor);
  page.replace("{tc}", _textColor);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEADER_END);

  if (scan) {
    // 判断是否扫描
    int n = WiFi.scanNetworks();
    DEBUG_WM(F("Scan done"));
    if (n == 0) {
      DEBUG_WM(F("No networks found"));
      page += F("找不到网络，请重新扫描.");
    } else {

      //sort networks
      int indices[n];
      for (int i = 0; i < n; i++) {
        indices[i] = i;
      }

      // RSSI SORT

      // old sort
      for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
          if (WiFi.RSSI(indices[j]) > WiFi.RSSI(indices[i])) {
            std::swap(indices[i], indices[j]);
          }
        }
      }

      /*std::sort(indices, indices + n, [](const int & a, const int & b) -> bool
        {
        return WiFi.RSSI(a) > WiFi.RSSI(b);
        });*/

      // remove duplicates ( must be RSSI sorted )
      if (_removeDuplicateAPs) {
        String cssid;
        for (int i = 0; i < n; i++) {
          if (indices[i] == -1) continue;
          cssid = WiFi.SSID(indices[i]);
          for (int j = i + 1; j < n; j++) {
            if (cssid == WiFi.SSID(indices[j])) {
              DEBUG_WM("DUP AP: " + WiFi.SSID(indices[j]));
              indices[j] = -1; // set dup aps to index -1
            }
          }
        }
      }

      //display networks in page
      for (int i = 0; i < n; i++) {
        if (indices[i] == -1) continue; // skip dups
        DEBUG_WM(WiFi.SSID(indices[i]));
        DEBUG_WM(WiFi.RSSI(indices[i]));
        int quality = getRSSIasQuality(WiFi.RSSI(indices[i]));

        if (_minimumQuality == -1 || _minimumQuality < quality) {
          String item = FPSTR(HTTP_ITEM);
          String rssiQ;
          rssiQ += quality;
          item.replace("{v}", WiFi.SSID(indices[i]));
          item.replace("{r}", rssiQ);
          if (WiFi.encryptionType(indices[i]) != ENC_TYPE_NONE) {
            item.replace("{i}", "l");
          } else {
            item.replace("{i}", "");
          }
          //DEBUG_WM(item);
          page += item;
          delay(0);
        } else {
          DEBUG_WM(F("Skipping due to quality"));
        }

      }
      page += "<br/>";
    }
  }

  page += FPSTR(HTTP_FORM_START);
  char parLength[5];
  // add the extra parameters to the form
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }

    String pitem = FPSTR(HTTP_FORM_PARAM);
    if (_params[i]->getID() != NULL) {
      pitem.replace("{i}", _params[i]->getID());
      pitem.replace("{n}", _params[i]->getID());
      pitem.replace("{p}", _params[i]->getPlaceholder());
      snprintf(parLength, 5, "%d", _params[i]->getValueLength());
      pitem.replace("{l}", parLength);
      DEBUG_WM(F("v:"));
      DEBUG_WM(_params[i]->getValue());
      pitem.replace("{v}", _params[i]->getValue());
      pitem.replace("{c}", _params[i]->getCustomHTML());
    } else {
      pitem = _params[i]->getCustomHTML();
    }

    page += pitem;
  }
  if (_params[0] != NULL) {
    page += "<br/>";
  }

  if (_sta_static_ip) {

    String item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "ip");
    item.replace("{n}", "ip");
    item.replace("{p}", "Static IP");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_ip.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "gw");
    item.replace("{n}", "gw");
    item.replace("{p}", "Static Gateway");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_gw.toString());

    page += item;

    item = FPSTR(HTTP_FORM_PARAM);
    item.replace("{i}", "sn");
    item.replace("{n}", "sn");
    item.replace("{p}", "Subnet");
    item.replace("{l}", "15");
    item.replace("{v}", _sta_static_sn.toString());

    page += item;

    page += "<br/>";
  }

  page += FPSTR(HTTP_FORM_END);
  page += FPSTR(HTTP_SCAN_LINK);

  page += FPSTR(HTTP_END);

  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent config page"));
}

/** Handle the WLAN save form and redirect to WLAN config page again */
/**
 * 功能描述：保存wifi设置
 */
void WiFiManager::handleWifiSave() {
  DEBUG_WM(F("WiFi save"));

  //SAVE/connect here
  _ssid = server->arg("s").c_str();
  _pass = server->arg("p").c_str();

  //parameters
  for (int i = 0; i < _paramsCount; i++) {
    if (_params[i] == NULL) {
      break;
    }
    //read parameter
    String value = server->arg(_params[i]->getID()).c_str();
    //store it in array
    value.toCharArray(_params[i]->_value, _params[i]->_length + 1);
    DEBUG_WM(F("Parameter"));
    DEBUG_WM(_params[i]->getID());
    DEBUG_WM(value);
  }

  if (server->arg("ip") != "") {
    DEBUG_WM(F("static ip"));
    DEBUG_WM(server->arg("ip"));
    //_sta_static_ip.fromString(server->arg("ip"));
    String ip = server->arg("ip");
    optionalIPFromString(&_sta_static_ip, ip.c_str());
  }
  if (server->arg("gw") != "") {
    DEBUG_WM(F("static gateway"));
    DEBUG_WM(server->arg("gw"));
    String gw = server->arg("gw");
    optionalIPFromString(&_sta_static_gw, gw.c_str());
  }
  if (server->arg("sn") != "") {
    DEBUG_WM(F("static netmask"));
    DEBUG_WM(server->arg("sn"));
    String sn = server->arg("sn");
    optionalIPFromString(&_sta_static_sn, sn.c_str());
  }

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Credentials Saved");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page.replace("{bc}", _backgroundColor);
  page.replace("{tc}", _textColor);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEADER_END);
  page += FPSTR(HTTP_SAVED);
  page += FPSTR(HTTP_END);

  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);
  DEBUG_WM(F("Sent wifi save page"));

  connect = true; //signal ready to connect/reset
}

/** Handle the info page */
void WiFiManager::handleInfo() {
  DEBUG_WM(F("Info"));

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page.replace("{bc}", _backgroundColor);
  page.replace("{tc}", _textColor);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEADER_END);
  page += F("<dl>");
  page += F("<dt>Chip ID</dt><dd>");
  page += ESP.getChipId();
  page += F("</dd>");
  page += F("<dt>Flash Chip ID</dt><dd>");
  page += ESP.getFlashChipId();
  page += F("</dd>");
  page += F("<dt>IDE Flash Size</dt><dd>");
  page += ESP.getFlashChipSize();
  page += F(" bytes</dd>");
  page += F("<dt>Real Flash Size</dt><dd>");
  page += ESP.getFlashChipRealSize();
  page += F(" bytes</dd>");
  page += F("<dt>Soft AP IP</dt><dd>");
  page += WiFi.softAPIP().toString();
  page += F("</dd>");
  page += F("<dt>Soft AP MAC</dt><dd>");
  page += WiFi.softAPmacAddress();
  page += F("</dd>");
  page += F("<dt>Station MAC</dt><dd>");
  page += WiFi.macAddress();
  page += F("</dd>");
  page += F("</dl>");
  page += FPSTR(HTTP_END);

  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent info page"));
}

/** Handle the reset page */
void WiFiManager::handleReset() {
  DEBUG_WM(F("Reset"));

  String page = FPSTR(HTTP_HEADER);
  page.replace("{v}", "Info");
  page += FPSTR(HTTP_SCRIPT);
  page += FPSTR(HTTP_STYLE);
  page.replace("{bc}", _backgroundColor);
  page.replace("{tc}", _textColor);
  page += _customHeadElement;
  page += FPSTR(HTTP_HEADER_END);
  page += F("模块会在几秒内进行重置，请稍等.");
  page += FPSTR(HTTP_END);

  server->sendHeader("Content-Length", String(page.length()));
  server->send(200, "text/html", page);

  DEBUG_WM(F("Sent reset page"));
  delay(5000);
  ESP.reset();
  delay(2000);
}

void WiFiManager::handleNotFound() {
  if (captivePortal()) { // If captive portal redirect instead of displaying the error page.
    return;
  }
  String message = "找不到页面\n\n";
  message += "URI: ";
  message += server->uri();
  message += "\nMethod: ";
  message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server->args();
  message += "\n";

  for ( uint8_t i = 0; i < server->args(); i++ ) {
    message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
  }
  server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Expires", "-1");
  server->sendHeader("Content-Length", String(message.length()));
  server->send ( 404, "text/plain", message );
}


/** Redirect to captive portal if we got a request for another domain. Return true in that case so the page handler do not try to handle the request again. */
boolean WiFiManager::captivePortal() {
  if (!isIp(server->hostHeader()) ) {
    DEBUG_WM(F("重定向到校验服务"));
    server->sendHeader("Location", String("http://") + toStringIp(server->client().localIP()), true);
    server->send ( 302, "text/plain", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
    server->client().stop(); // Stop is needed because we sent no content length
    return true;
  }
  return false;
}

//start up config portal callback
void WiFiManager::setAPCallback( void (*func)(WiFiManager* myWiFiManager) ) {
  _apcallback = func;
}

//start up save config callback
void WiFiManager::setSaveConfigCallback( void (*func)(void) ) {
  _savecallback = func;
}

//sets a custom element to add to head, like a new style tag
/**
 * 功能描述：添加一个自定义的element到页面头部 比如加个头像
 * @param element element样式的字符串
 */
void WiFiManager::setCustomHeadElement(const char* element) {
  _customHeadElement = element;
}

void WiFiManager::setHeadImgBase64(String imgBase64) {
  _imgBase64= imgBase64;
}

void WiFiManager::setButtonBackground(const char* backgroundColor){
  _backgroundColor= backgroundColor;
}

void WiFiManager::setButtonTextColor(const char* textColor) {
  _textColor= textColor;
}

void WiFiManager::setPageTitle(const char* pageTitle){
  _pageTitle = pageTitle;
}

//if this is true, remove duplicated Access Points - defaut true
void WiFiManager::setRemoveDuplicateAPs(boolean removeDuplicates) {
  _removeDuplicateAPs = removeDuplicates;
}



template <typename Generic>
void WiFiManager::DEBUG_WM(Generic text) {
  if (_debug) {
    Serial.print("*WM: ");
    Serial.println(text);
  }
}

int WiFiManager::getRSSIasQuality(int RSSI) {
  int quality = 0;

  if (RSSI <= -100) {
    quality = 0;
  } else if (RSSI >= -50) {
    quality = 100;
  } else {
    quality = 2 * (RSSI + 100);
  }
  return quality;
}

/** Is this an IP? */
boolean WiFiManager::isIp(String str) {
  for (size_t i = 0; i < str.length(); i++) {
    int c = str.charAt(i);
    if (c != '.' && (c < '0' || c > '9')) {
      return false;
    }
  }
  return true;
}

/** IP to String? */
String WiFiManager::toStringIp(IPAddress ip) {
  String res = "";
  for (int i = 0; i < 3; i++) {
    res += String((ip >> (8 * i)) & 0xFF) + ".";
  }
  res += String(((ip >> 8 * 3)) & 0xFF);
  return res;
}
