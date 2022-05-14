#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#ifndef STASSID
#define STASSID "du_Toit"
#define STAPSK "Master101!"
#endif

const char *ssid = STASSID;
const char *password = STAPSK;
const char *host = "LogicAnalyzer";

volatile int sampleAmount = 1000;
volatile int bufferSize = sampleAmount * 9 + 8;

// what pins to use, between 0 and 15
static const int PIN0 = D2; //4; //D2
static const int PIN1 = D1; //5; //D1
static const int PIN2 = D6; //12; //D6
static const int PIN3 = D5; //14; //D5
// unused pins should be tied to the ground

static const int CLOCK_PERIOD = 100000;
volatile int countClock = 0;
volatile int clockState = 0;
static const int OUT0 = D7; //13; //D7;
static const int OUT1 = D8; //15; //D8;

static_assert(PIN0 >= 0 && PIN0 < 16, "");
static_assert(PIN1 >= 0 && PIN1 < 16, "");
static_assert(PIN2 >= 0 && PIN2 < 16, "");
static_assert(PIN3 >= 0 && PIN3 < 16, "");

static constexpr uint32_t MASK = (1 << PIN0) | (1 << PIN1) | (1 << PIN2) | (1 << PIN3);

volatile int ledState = 1;
volatile int pwmDuty = 128;

char buffer[5000];
uint8_t scale[] = {0, 8, 16, 24, 32, 40, 48, 56};

int port = 8888; //Port number
WiFiServer server(port);
WiFiClient client;

void setSampleAmount(int sampleAmountTemp)
{
  sampleAmount = sampleAmountTemp;
  bufferSize = sampleAmount * 9 + 8;
}

void setupPins()
{
  // pinMode(OUT0, OUTPUT);
  analogWrite(OUT0, pwmDuty);
  pinMode(OUT1, OUTPUT);
}

void setupAnalyzer()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  pinMode(PIN0, INPUT);
  pinMode(PIN1, INPUT);
  pinMode(PIN2, INPUT);
  pinMode(PIN3, INPUT);
}

// uint8_t times[N_SAMPLES*8]; // when did change happen
// uint8_t values[N_SAMPLES*4];     // GPI value at time




extern void ICACHE_RAM_ATTR collect()
{
  volatile uint32_t index = 0;
  for (size_t i = 0; i < 4; i++)
  {
    buffer[index++] = sampleAmount >> scale[i];
  }
  volatile uint64_t time = micros();
  volatile uint64_t time2;
  for (size_t i = 0; i < 8; i++)
  {
    buffer[index++] = time >> scale[i];
  }

  volatile int value = GPI & MASK;
  volatile int prevValue = value;
  buffer[index++] = compactValue(value);

  for (int i = 1; i < sampleAmount; ++i)
  {
    do
    {
      value = GPI & MASK;
    } while (value == prevValue);
    prevValue = value;

    time2 = micros() - time;
    for (size_t i = 0; i < 8; i++)
    {
      buffer[index++] = time2 >> scale[i];
    }
    // values[i] = value;
    buffer[index++] = compactValue(value);
  }
}

uint8_t compactValue(uint32_t value)
{
  int res = 0;
  if ((value & (1 << PIN0)) != 0)
  {
    res |= (1 << 0);
  }
  if ((value & (1 << PIN1)) != 0)
  {
    res |= (1 << 1);
  }
  if ((value & (1 << PIN2)) != 0)
  {
    res |= (1 << 2);
  }
  if ((value & (1 << PIN3)) != 0)
  {
    res |= (1 << 3);
  }
  return res;
}

void report()
{
  // client.write('S');
  // client.write(compactValue(values[0]));
  // client.write(':');
  // client.write(N_SAMPLES - 1);

  // for (int i = 1; i < N_SAMPLES; ++i) {
  //   client.write(':');
  //   client.write(compactValue(values[i]));
  //   client.write(':');
  //   client.write(times[i] - times[0]);
  // }
  // client.write('e');
  client.write('S');
  client.write(buffer,bufferSize);
  client.write('e');
}

void loopAnalyzer()
{
  digitalWrite(LED_BUILTIN, LOW);
  ESP.wdtDisable();
  collect();
  ESP.wdtEnable(WDTO_8S);
  digitalWrite(LED_BUILTIN, HIGH);
}

void setup()
{
  Serial.begin(115200);
  setupWifi();
  setupOta();
  setupPins();
  setupAnalyzer();
}

void setupWifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Web server started!");
}

void setupOta()
{
  Serial.println("setupOta");

  ArduinoOTA.setHostname(host);
  ArduinoOTA.onStart([]() { // switch off all the PWMs during upgrade
    Serial.println("onStart");
  });

  ArduinoOTA.onEnd([]() { // do a fancy thing with our board led at end
    Serial.println("onEnd");
  });

  ArduinoOTA.onError([](ota_error_t error)
                     {
                       (void)error;
                       Serial.println("onError");
                       ESP.restart();
                     });

  /* setup the OTA server */
  ArduinoOTA.begin();
  Serial.println("Ready");
}

void loopSocket()
{
  client = server.available();

  if (client)
  {
    if (client.connected())
    {
      Serial.println("Client Connected");
    }

    while (client.connected())
    {
      while (client.available() > 0)
      {
        // read data from the connected client
        int readChar = client.read();
        Serial.write(readChar);
        if (readChar == 's')
        {
          Serial.println("starting");
          uint8_t samples[2];
          int readSampleSize = client.readBytes(samples,2);
          int sampleSize = samples[1] << 8 | samples[0];
          setSampleAmount(sampleSize);
          Serial.print("sampleSize: ");
          Serial.println(sampleSize);
          loopAnalyzer();
          client.write('d');
        }
        if (readChar == 't')
        {
          ledState = ledState ^ 1;
          digitalWrite(LED_BUILTIN, ledState);
          client.print(ledState);
        }
        if (readChar == 'p')
        {
          pwmDuty = pwmDuty + 10;
          analogWrite(OUT0, pwmDuty);
          client.print(pwmDuty);
        }
        if (readChar == 'm')
        {
          pwmDuty = pwmDuty - 10;
          analogWrite(OUT0, pwmDuty);
          client.print(pwmDuty);
        }
        if (readChar == 'r')
        {
          report();
        }
      }
      //Send Data to connected client
      // while (Serial.available() > 0)
      // {
      //   client.write(Serial.read());
      // }
    }
    client.stop();
    Serial.println("Client disconnected");
  }
}

void loopPins()
{
  countClock++;
  if (countClock > CLOCK_PERIOD)
  {
    countClock = 0;
    clockState = clockState ^ 1;
    if (clockState == 1)
    {
      digitalWrite(OUT0, HIGH);
      digitalWrite(OUT1, LOW);
    }
    else
    {
      digitalWrite(OUT0, LOW);
      digitalWrite(OUT1, HIGH);
    }
  }
}

void loop()
{
  ArduinoOTA.handle();
  loopSocket();
  // loopPins();
}
