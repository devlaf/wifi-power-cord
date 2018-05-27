#include <stdlib.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define wifi_ssid "your_wifi_ssid"
#define wifi_password "your_wifi_password"

#define mqtt_server "your_mqtt_server_name"
#define mqtt_server_port 1883
#define mqtt_user "your_mqtt_username"
#define mqtt_password "your_mqtt_password"
#define mqtt_outlet_topic "outlet_relays"

#define pin_relay_signal 4
#define pin_net_id_bit_3 14
#define pin_net_id_bit_2 12
#define pin_net_id_bit_1 13
#define pin_net_id_bit_0 15

WiFiClient wifi_client;
PubSubClient mqtt_client;
char* hostname;
bool relay_state;

void setup()
{
  Serial.begin(9600);

  pinMode(pin_relay_signal, OUTPUT);
  pinMode(pin_net_id_bit_3, INPUT);
  pinMode(pin_net_id_bit_2, INPUT);
  pinMode(pin_net_id_bit_1, INPUT);
  pinMode(pin_net_id_bit_0, INPUT);

  set_relay(false);

  set_hostname();

  setup_wifi();

  while(!ensure_mqtt_connection()) {
    delay(5000);
  }
}

void loop()
{
  ensure_mqtt_connection();

  mqtt_client.loop();

  delay(3000);
}

void set_hostname()
{
  int id = read_dip_id();

  size_t len = snprintf(NULL, 0, "esp8266-%d", id);
  hostname = (char*)malloc(len+1);
  sprintf(hostname, "esp8266-%d", id);

  Serial.print("Setting host name to ");
  Serial.println(hostname);

  WiFi.hostname(hostname);
}

int read_dip_id()
{
  int bit_3 = digitalRead(pin_net_id_bit_3);
  int bit_2 = digitalRead(pin_net_id_bit_2);
  int bit_1 = digitalRead(pin_net_id_bit_1);
  int bit_0 = digitalRead(pin_net_id_bit_0);

  return (8 * bit_3) + (4 * bit_2) + (2 * bit_1) + bit_0;
}

void setup_wifi()
{
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);

  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("*");
  }

  Serial.print("WiFi connected at IP Address: ");
  Serial.println(WiFi.localIP());
}

bool ensure_mqtt_connection()
{
  if (mqtt_client.connected())
    return true;

  Serial.print("Connecting to MQTT server...");

  mqtt_client.setClient(wifi_client);
  mqtt_client.setServer(mqtt_server,mqtt_server_port);
  mqtt_client.setCallback(on_mqtt_msg);

  if (mqtt_client.connect(hostname, mqtt_user, mqtt_password)) {
    Serial.println("connected.");
    mqtt_client.subscribe(mqtt_outlet_topic);
    return true;
  }

  Serial.print("connection failed, status = ");
  Serial.println(mqtt_client.state());
  return false;
}

void on_mqtt_msg(char* topic, byte* payload, unsigned int len)
{
  if (strcmp(topic, mqtt_outlet_topic) != 0)
    return;

  char* msg = (char*)malloc(len);
  memcpy(msg, payload, len);

  char** parsed = parse_msg_payload(msg);

  if (strcmp(parsed[1], hostname) != 0)
    goto cleanup;

  if (strcmp(parsed[0], "state_request") == 0)
    send_state_msg();

  if (strcmp(parsed[0], "desired_state") == 0)
    set_relay(strcmp(parsed[2], "1") == 0 ? true : false);

  cleanup:
    for (int i = 0; parsed[i]; i++)
        free(parsed[i]);
    free(parsed);
    free(msg);
}

void send_state_msg()
{
  char* to_publish = serialize_current_state();
  mqtt_client.publish(mqtt_outlet_topic, to_publish, true);
  free(to_publish);
}

void set_relay(bool engage)
{
  Serial.println("Toggling relay");

  relay_state = engage;

  if (engage)
    digitalWrite(pin_relay_signal, HIGH);
  else
    digitalWrite(pin_relay_signal, LOW);
}

char* serialize_current_state()
{
  size_t len = snprintf(NULL, 0, "current_state|%s|0|", hostname);
  char* buf = (char*)malloc(len+1);
  sprintf(buf, "current_state|%s|%s", hostname, relay_state ? "1" : "0");
  return buf;
}

char** parse_msg_payload(char* payload)
{
  const char* delim = "|";

  size_t len = character_count(payload, delim[0]) + 3;

  char** result = (char**) malloc(sizeof(char*) * len);

  if (result) {
    size_t idx  = 0;
    char* token = strtok(payload, delim);

    while (token) {
      *(result + idx++) = strdup(token);
      token = strtok(0, delim);
    }
    *(result + idx) = 0;
  }

  return result;
}

int character_count(char* msg, const char character)
{
  int count = 0;

  char* tmp = msg;
  while (*tmp) {
    if (character == *tmp)
      count++;
    tmp++;
  }

  return count;
}
