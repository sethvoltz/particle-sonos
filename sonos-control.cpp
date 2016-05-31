/*
* ==================================================================================================
* Inspired by and borrowing from the following sources:
* - Arduino sketch for infrared control of Sonos ZonePlayer (c) 2010 Simon Long, KuDaTa Software
* - Original Sonos control Arduino sketch by Jan-Piet Mens.
*
* Use is at the user's own risk - no warranty is expressed or implied.
* ==================================================================================================
*/

#include <string>
#include <vector>
#include <sstream>
#include <iterator>

#include "application.h"

// ---------------------------------------------------------------------------- Macros and Constants
#define TO_STRING(x) static_cast< std::ostringstream & >(( std::ostringstream() << std::dec << x )).str()

/* Sonos SOAP command packet enumeration */
#define SONOS_PAUSE 0
#define SONOS_PLAY 1
#define SONOS_PREV 2
#define SONOS_NEXT 3
#define SONOS_SEEK 4
#define SONOS_NORMAL 5
#define SONOS_REPEAT 6
#define SONOS_SHUFF 7
#define SONOS_SHUREP 8
#define SONOS_MODE 9
#define SONOS_POS_INFO 10
#define SONOS_GETVOL 11
#define SONOS_SETVOL 12

// Config defaults and sizes
#define DEVICE_NAME "unknown device"
#define DEVICE_NAME_SIZE 65
#define DEVICE_SONOS_IP 0
#define DEVICE_SONOS_IP_SIZE 15

/* Enable DEBUG for serial debug output */
//#define DEBUG

// --------------------------------------------------------------------------------------- Templates
const std::string sonos_command_template =
  "POST /MediaRenderer/{{SERVICE_NAME}}/Control HTTP/1.1\r\n"
  "Connection: close\r\n"
  "Host: {{IP_ADDRESS}}:1400\r\n"
  "Content-Length: {{CONTENT_LENGTH}}\r\n" // 231 + 2 * strlen(COMMAND_NAME) + strlen(EXTRA_DATA) + strlen(SERVICE_NAME));
  "Content-Type: text/xml; charset=\"utf-8\"\r\n"
  "Soapaction: \"urn:schemas-upnp-org:service:{{SERVICE_NAME}}:1#{{COMMAND_NAME}}\"\r\n"
  "\r\n"
  "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
  "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
  "<s:Body>"
  "<u:{{COMMAND_NAME}} xmlns:u=\"urn:schemas-upnp-org:service:{{SERVICE_NAME}}:1\">"
  "<InstanceID>0</InstanceID>"
  "{{EXTRA_DATA}}"
  "</u:{{COMMAND_NAME}}>"
  "</s:Body>"
  "</s:Envelope>\r\n"

// --------------------------------------------------------------------------------- Runtime Globals


bool button_press_flag = false;

int newvol;             // Global used for volume setting
char nullbuf[1] = {0};  // Global null buffer used to disable data reception

// Buffers used for Sonos data reception
char data1[20];
char data2[20];

// Network
TCPClient client;
IPAddress ip_address; // IP address of ZonePlayer to control


// ---------------------------------------------------------------------------------- EEPROM Storage
#define CONFIG_VERSION "st1"
#define CONFIG_START 0

// storage data
struct ConfigStruct {
    char version[4];
    char device_name[DEVICE_NAME_SIZE];
    char device_uuid[DEVICE_SONOS_IP_SIZE];
} config = {
    CONFIG_VERSION,
    DEVICE_NAME,
    DEVICE_SONOS_IP
};

// Load configuration
void loadConfig() {
  if (EEPROM.read(CONFIG_START + 0) == CONFIG_VERSION[0] &&
      EEPROM.read(CONFIG_START + 1) == CONFIG_VERSION[1] &&
      EEPROM.read(CONFIG_START + 2) == CONFIG_VERSION[2]) {
    for (unsigned int t = 0; t < sizeof(config); t++) {
      *((char*)&config + t) = EEPROM.read(CONFIG_START + t);
    }
  }
}

// Save configuration
void saveConfig() {
  for (unsigned int t = 0; t < sizeof(config); t++)
    EEPROM.write(CONFIG_START + t, *((char*)&config + t));
}

// -------------------------------------------------------------------------------- Helper Functions
void debug(std::string message) {
  if (ENABLE_DEBUG == 1) {
    Serial.println(message.c_str());
    Particle.publish("DEBUG", message.c_str());
  }
}

// Kudos: http://stackoverflow.com/a/27658515
std::string replaceAll(
  const std::string& str,
  const std::string& find,
  const std::string& replace
) {
  using namespace std;
  std::string result;
  size_t find_len = find.size();
  size_t pos,from=0;
  while (std::string::npos != (pos=str.find(find,from))) {
    result.append(str, from, pos-from);
    result.append(replace);
    from = pos + find_len;
  }
  result.append(str, from, std::string::npos);
  return result;
}


// ------------------------------------------------------------------------ Particle Cloud Functions
int call_setDeviceName(String name) {
    //update new value to eeprom
    name.toCharArray(config.device_name, DEVICE_NAME_SIZE);
    saveConfig();

    config.device_uuid[0] = 0;
    device_uuid = getDeviceUUID();

    std::stringstream ss;
    ss << "Update Name: '" << config.device_name << "'";
    ss << ", UUID: " << config.device_uuid;
    debug(ss.str());

    return 1;
}

int call_setSonosIp(String ip) {
  //update new value to eeprom
  name.toCharArray(config.device_name, DEVICE_NAME_SIZE);
  saveConfig();

  config.device_uuid[0] = 0;
  device_uuid = getDeviceUUID();

  std::stringstream ss;
  ss << "Update Name: '" << config.device_name << "'";
  ss << ", UUID: " << config.device_uuid;
  debug(ss.str());

  return 1;
}

// ----------------------------------------------------------------------------------- SOAP Handlers

void sendSearchReply() {
  debug("Sending UPnP Reply to multicast group");
  // Thanks to https://github.com/smpickett/particle_ssdp_server
  udp.beginPacket(udp.remoteIP(), udp.remotePort());

  char ip_string[24];
  sprintf(ip_string, "%d.%d.%d.%d", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);

  std::string wemo_reply;
  wemo_reply = replaceAll(wemo_reply_template, "{{CACHE_INTERVAL}}", TO_STRING(cache_interval));
  wemo_reply = replaceAll(wemo_reply, "{{TIMESTAMP}}", getTimestamp());
  wemo_reply = replaceAll(wemo_reply, "{{IP_ADDRESS}}", ip_string);
  wemo_reply = replaceAll(wemo_reply, "{{WEB_PORT}}", TO_STRING(web_port));
  wemo_reply = replaceAll(wemo_reply, "{{UUID}}", device_uuid);
  wemo_reply = replaceAll(wemo_reply, "{{SERIAL_NUMBER}}", device_serial);

  udp.write(wemo_reply.c_str());
  udp.endPacket();
}


// --------------------------------------------------------------------------------- Setup Functions
void setup() {
}


// --------------------------------------------------------------------------------- Main Event Loop
void loop() {
  int res;

  // switch (eventtype) {
  //   case REMOTE_PLAY:
  //     mode = MODE_NORMAL;
  //     sonos(SONOS_PLAY, nullbuf, nullbuf);
  //     break;
  //
  //   case REMOTE_PAUSE:
  //     mode = MODE_NORMAL;
  //     sonos(SONOS_PAUSE, nullbuf, nullbuf);
  //     break;
  //
  //   case REMOTE_SHUFFLE:
  //     mode = MODE_NORMAL;
  //     sonos(SONOS_MODE, data1, nullbuf);
  //
  //     res = sum_letters(data1 + 1);
  //     if (res == 457) sonos(SONOS_SHUFF, nullbuf, nullbuf);
  //
  //     //NORMAL
  //     if (res == 525) sonos(SONOS_REPEAT, nullbuf, nullbuf);
  //
  //     //SHUFFLE
  //     if (res == 761) sonos(SONOS_SHUREP, nullbuf, nullbuf);
  //
  //     //REPEAT_ALL
  //     if (res == 1226) sonos(SONOS_NORMAL, nullbuf, nullbuf);
  //
  //     //SHUFFLE_NOREPEAT
  //     break;
  //
  //   case REMOTE_VOLU:
  //     sonos(SONOS_GETVOL, data1, nullbuf);
  //     sscanf(data1 + 1, "%d", &newvol);
  //
  //     newvol += 5;
  //     if (newvol > 100) newvol = 100;
  //     sonos(SONOS_SETVOL, nullbuf, nullbuf);
  //
  //     break;
  //
  //   case REMOTE_VOLD:
  //     sonos(SONOS_GETVOL, data1, nullbuf);
  //     sscanf(data1 + 1, "%d", &newvol);
  //
  //     newvol -= 5;
  //     if (newvol < 0) newvol = 0;
  //     sonos(SONOS_SETVOL, nullbuf, nullbuf);
  //
  //     break;
  //   default:
  //     break;
  // }
}

/*----------------------------------------------------------------------*/
/* sonos - sends a command packet to the ZonePlayer */
void sonos(int cmd, char *resp1, char *resp2) {
  //char buf[512];
  char buf[380];
  char cmdbuf[32];
  char extra[64];
  char service[20];
  char *ptr1;
  char *ptr2;
  char *optr;
  char copying;
  unsigned long timeout;

  extra[0] = 0;
  strcpy(service, "AVTransport");

  //delay(2000);
  if (client.connect(sonosip, 1400)) {
    #ifdef DEBUG
    Serial.println("connected");
    #endif

    // Prepare the data strings to go into the desired command packet
    switch (cmd) {
      case SONOS_PLAY:
        strcpy(cmdbuf, "Play");
        strcpy(extra, "<Speed>1</Speed>");
        break;

      case SONOS_PAUSE:
        strcpy(cmdbuf, "Pause");
        break;

      case SONOS_PREV:
        strcpy(cmdbuf, "Previous");
        break;

      case SONOS_NEXT:
        strcpy(cmdbuf, "Next");
        break;

      case SONOS_SEEK:
        strcpy(cmdbuf, "Seek");
        sprintf(extra, "<Unit>REL_TIME</Unit><Target>%02d:%02d:%02d</Target>", desttime / 3600, (desttime / 60) % 60, desttime % 60);
        break;

      case SONOS_NORMAL:
      case SONOS_REPEAT:
      case SONOS_SHUFF:
      case SONOS_SHUREP:
        if (cmd == SONOS_NORMAL)
          strcpy(cmdbuf, "NORMAL");

        if (cmd == SONOS_REPEAT)
          strcpy(cmdbuf, "REPEAT_ALL");

        if (cmd == SONOS_SHUFF)
          strcpy(cmdbuf, "SHUFFLE_NOREPEAT");

        if (cmd == SONOS_SHUREP)
          strcpy(cmdbuf, "SHUFFLE");

        sprintf(extra, "<NewPlayMode>%s</NewPlayMode>", cmdbuf);
        strcpy(cmdbuf, "SetPlayMode");
        break;

      case SONOS_MODE:
        strcpy(cmdbuf, "GetTransportSettings");
        strcpy(resp1, "PlayMode");
        break;

      case SONOS_POS_INFO:
        strcpy(cmdbuf, "GetPositionInfo");
        strcpy(resp1, "RelTime");
        break;

      case SONOS_GETVOL:
        strcpy(cmdbuf, "GetVolume");
        strcpy(extra, "<Channel>Master</Channel>");
        strcpy(service, "RenderingControl");
        strcpy(resp1, "CurrentVolume");
        break;

      case SONOS_SETVOL:
        strcpy(cmdbuf, "SetVolume");
        sprintf(extra, "<Channel>Master</Channel><DesiredVolume>%d</DesiredVolume>", newvol);
        strcpy(service, "RenderingControl");
        break;
    }

    // Output the command packet
    // sprintf(buf, "POST /MediaRenderer/%s/Control HTTP/1.1\r\n", service);
    // out(buf);
    // out("Connection: close\r\n");
    // sprintf(buf, "Host: %d.%d.%d.%d:1400\r\n", sonosip[0], sonosip[1], sonosip[2], sonosip[3]);
    // out(buf);
    // sprintf(buf, "Content-Length: %d\r\n", 231 + 2 * strlen(cmdbuf) + strlen(extra) + strlen(service));
    // out(buf);
    // out("Content-Type: text/xml; charset=\"utf-8\"\r\n");
    // sprintf(buf, "Soapaction: \"urn:schemas-upnp-org:service:%s:1#%s\"\r\n", service, cmdbuf);
    // out(buf);
    // out("\r\n");
    // sprintf(buf, "%s<u:%s%s%s%s%s</u:%s>%s\r\n", SONOS_CMDH, cmdbuf, SONOS_CMDP, service, SONOS_CMDQ, extra, cmdbuf, SONOS_CMDF);
    // out(buf);

    // TODO: Use series of replaceAll() to build up the template to send

    // wait for a response packet
    timeout = millis();
    while ((!client.available()) && ((millis() - timeout) < 1000));

    // parse the response looking for the strings in resp1 and resp2
    ptr1 = resp1;
    ptr2 = resp2;
    copying = 0;

    while (client.available()) {
      char c = client.read();
      // if response buffers start with nulls, either no response required, or already received
      if (resp1[0] || resp2[0]) {
        // if a response has been identified, copy the data
        if (copying) {
          // look for the < character that indicates the end of the data
          if (c == '<') {
            // stop receiving data, and null the first character in the response buffer
            copying = 0;
            *optr = 0;
            if (copying == 1)
              resp1[0] = 0;
            else
              resp2[0] = 0;
          } else {
            // copy the next byte to the response buffer
            *optr = c;
            optr++;
          }
        } else {
          // look for input characters that match the response buffers
          if (c == *ptr1) {
            // character matched - advance to next character to match
            ptr1++;
            // is this the end of the response buffer
            if (*ptr1 == 0) {
              // string matched - start copying from next character received
              copying = 1;
              optr = resp1;
              ptr1 = resp1;
            }
          } else {
            ptr1 = resp1;
          }
          // as above for second response buffer
          if (c == *ptr2) {
            ptr2++;
            if (*ptr2 == 0) {
              copying = 2;
              optr = resp2;
              ptr2 = resp2;
            }
          } else {
            ptr2 = resp2;
          }
        }
      }

      #ifdef DEBUG
      Serial.print(c);
      #endif
    }
  } else {
    #ifdef DEBUG
    Serial.println("connection failed");
    #endif
  }

  while (client.available()) client.read();

  delay(100);
  client.stop();
}


// ------------------------------------------------------------------ Interrupts
void buttonPressInterrupt () {
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();

  if (interrupt_time - last_interrupt_time > 50) { // debounce time = 50ms
    button_press_flag = true;
  }
  last_interrupt_time = interrupt_time;
}

void checkButtonPress () {
  if (button_press_flag == true) {
    debug("Button pressed, toggling device state");
    if (device_state == 0) {
      turnDeviceOn();
    } else {
      turnDeviceOff();
    }
    button_press_flag = false;
  }
}
