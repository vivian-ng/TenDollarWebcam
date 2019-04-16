#include "OV2640.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <AutoWifi.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>

#include "SimStreamer.h"
#include "OV2640Streamer.h"
#include "OV2640.h"
#include "CRtspSession.h"


#define HOSTNAME "esp32cam"  // allows access by hostname.local

// This board has slightly different GPIO bindings (and lots more RAM)
// uncomment to use
// #define USEBOARD_TTGO_T

#define USEBOARD_AITHINKER

#ifndef USEBOARD_AITHINKER
#define ENABLE_OLED //if want use oled ,turn on thi macro
#endif

// #define SOFTAP_MODE // If you want to run our own softap turn this on
#define ENABLE_WEBSERVER
#define ENABLE_RTSPSERVER


#ifndef USEBOARD_AITHINKER
// If your board has a GPIO which is attached to a button, uncomment the following line
// and adjust the GPIO number as needed.  If that button is held down during boot the device
// will factory reset.
#define FACTORYRESET_BUTTON 32
#endif

#ifdef ENABLE_OLED
#include "SSD1306.h"
#define OLED_ADDRESS 0x3c

#ifdef USEBOARD_TTGO_T
#define I2C_SDA 21
#define I2C_SCL 22
#else
#define I2C_SDA 14
#define I2C_SCL 13
#endif
SSD1306Wire display(OLED_ADDRESS, I2C_SDA, I2C_SCL, GEOMETRY_128_32);
bool hasDisplay; // we probe for the device at runtime
#endif

OV2640 cam;

#ifdef ENABLE_WEBSERVER
WebServer server(80);
#endif

#ifdef ENABLE_RTSPSERVER
WiFiServer rtspServer(8554);
#endif


#ifdef SOFTAP_MODE
IPAddress apIP = IPAddress(192, 168, 1, 1);
#else
#endif

#ifdef ENABLE_WEBSERVER
void handle_jpg_stream(void)
{
    WiFiClient client = server.client();
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
    server.sendContent(response);

    while (1)
    {
        cam.run();
        if (!client.connected())
            break;
        response = "--frame\r\n";
        response += "Content-Type: image/jpeg\r\n\r\n";
        server.sendContent(response);

        client.write((char *)cam.getfb(), cam.getSize());
        server.sendContent("\r\n");
        if (!client.connected())
            break;
    }
}

void handle_jpg(void)
{
    WiFiClient client = server.client();

    cam.run();
    if (!client.connected())
    {
        return;
    }
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-disposition: inline; filename=capture.jpg\r\n";
    response += "Content-type: image/jpeg\r\n\r\n";
    server.sendContent(response);
    client.write((char *)cam.getfb(), cam.getSize());
}

void handleNotFound()
{
    String message = "Server is running!\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    server.send(200, "text/plain", message);
}
#endif

void lcdMessage(String msg)
{
  #ifdef ENABLE_OLED
    if(hasDisplay) {
        display.clear();
        display.drawString(128 / 2, 32 / 2, msg);
        display.display();
    }
  #endif
}

void setup()
{
  #ifdef ENABLE_OLED
    hasDisplay = display.init();
    if(hasDisplay) {
        display.flipScreenVertically();
        display.setFont(ArialMT_Plain_16);
        display.setTextAlignment(TEXT_ALIGN_CENTER);
    }
  #endif
    lcdMessage("booting");

    Serial.begin(115200);
    while (!Serial)
    {
        ;
    }

    int camInit =
#ifdef USEBOARD_TTGO_T
        cam.init(esp32cam_ttgo_t_config);
#else
#ifdef USEBOARD_AITHINKER
        cam.init(esp32cam_aithinker_config);
#else
        cam.init(esp32cam_config);
#endif
#endif
    Serial.printf("Camera init returned %d\n", camInit);

    IPAddress ip;


#ifdef SOFTAP_MODE
    const char *hostname = "devcam";
    // WiFi.hostname(hostname); // FIXME - find out why undefined
    lcdMessage("starting softAP");
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    bool result = WiFi.softAP(hostname, "12345678", 1, 0);
    if (!result)
    {
        Serial.println("AP Config failed.");
        return;
    }
    else
    {
        Serial.println("AP Config Success.");
        Serial.print("AP MAC: ");
        Serial.println(WiFi.softAPmacAddress());

        ip = WiFi.softAPIP();
    }
#else

    WiFi.mode(WIFI_STA);


    AutoWifi a;

    #ifdef FACTORYRESET_BUTTON
    pinMode(FACTORYRESET_BUTTON, INPUT);
    if(!digitalRead(FACTORYRESET_BUTTON))     // 1 means not pressed
        a.resetProvisioning();
    #endif

    if(!a.isProvisioned())
        lcdMessage("Setup wifi!");
    else
        lcdMessage(String("join ") + a.getSSID());

    a.startWifi();

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(F("."));
    }
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    // ArduinoOTA.setHostname("myesp32");

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
      })
      .onEnd([]() {
        Serial.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
      });

    ArduinoOTA.begin();

    ip = WiFi.localIP();
    Serial.println(F("WiFi connected"));
    Serial.println(ip);

    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin(HOSTNAME)) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");
#endif

    lcdMessage(ip.toString());

#ifdef ENABLE_WEBSERVER
    server.on("/", HTTP_GET, handle_jpg_stream);
    server.on("/jpg", HTTP_GET, handle_jpg);
    server.onNotFound(handleNotFound);
    server.begin();
#endif

#ifdef ENABLE_RTSPSERVER
    rtspServer.begin();
#endif
}

CStreamer *streamer;
CRtspSession *session;
WiFiClient client; // FIXME, support multiple clients

void loop()
{
    ArduinoOTA.handle();
#ifdef ENABLE_WEBSERVER
    server.handleClient();
#endif

#ifdef ENABLE_RTSPSERVER
    uint32_t msecPerFrame = 100;
    static uint32_t lastimage = millis();

    // If we have an active client connection, just service that until gone
    // (FIXME - support multiple simultaneous clients)
    if(session) {
        session->handleRequests(0); // we don't use a timeout here,
        // instead we send only if we have new enough frames

        uint32_t now = millis();
        if(now > lastimage + msecPerFrame || now < lastimage) { // handle clock rollover
            session->broadcastCurrentFrame(now);
            lastimage = now;

            // check if we are overrunning our max frame rate
            now = millis();
            if(now > lastimage + msecPerFrame)
                printf("warning exceeding max frame rate of %d ms\n", now - lastimage);
        }

        if(session->m_stopped) {
            delete session;
            delete streamer;
            session = NULL;
            streamer = NULL;
        }
    }
    else {
        client = rtspServer.accept();

        if(client) {
            //streamer = new SimStreamer(&client, true);             // our streamer for UDP/TCP based RTP transport
            streamer = new OV2640Streamer(&client, cam);             // our streamer for UDP/TCP based RTP transport

            session = new CRtspSession(&client, streamer); // our threads RTSP session and state
        }
    }
#endif
}
