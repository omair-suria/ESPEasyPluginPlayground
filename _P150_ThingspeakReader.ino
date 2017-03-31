//#######################################################################################################
//#################################### Plugin 150: ThingspeakReader #####################################
//#######################################################################################################

#define PLUGIN_150
#define PLUGIN_ID_150         150
#define PLUGIN_NAME_150       "ThingspeakCMDReader"
#define PLUGIN_VALUENAME1_150 "lastRead"

#define THINGSPEAK_READ_DEFAULT_DELAY  30 //in seconds
#define THINGSPEAK_READ_TEMPORARY_DELAY_TIMEOUT 300 //in seconds

unsigned long thingspeakRead_timer;
String thingspeakRead_prev_cmd;
byte thingspeakRead_default_delay;
byte thingspeakRead_current_delay;
unsigned thingspeakRead_temporary_delay_timer;
boolean thingspeakRead_temporary_delay_active;

boolean Plugin_150(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {

    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_150;
        Device[deviceCount].Type = DEVICE_TYPE_DUMMY;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = false;
        Device[deviceCount].DecimalsOnly = false;
        Device[deviceCount].ValueCount = 1;
        Device[deviceCount].SendDataOption = false;
        Device[deviceCount].TimerOption = true;
        Device[deviceCount].TimerOptional = false;
        Device[deviceCount].GlobalSyncOption = true;        
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_150);
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_150));
        break;
      }

//    case PLUGIN_WEBFORM_LOAD:
//      {
//        success = true;
//        break;
//      }
//
//    case PLUGIN_WEBFORM_SAVE:
//      {
//        success = true;
//        break;
//      }
      
//    case PLUGIN_READ:
//      {
//        success = true;
//        break;
//      }

    case PLUGIN_WRITE:
      {
        String command = parseString(string, 1);

        if(command == F("thingspeak"))
        {
          success = true;

          if(parseString(string,2) == F("tmpdelay"))
          {
            if(event->Par2 >= 5 && event->Par2 <=30)
            {
              thingspeakRead_current_delay = event->Par2;
              thingspeakRead_timer = millis() + thingspeakRead_current_delay*1000;
              thingspeakRead_temporary_delay_active = true;
              thingspeakRead_temporary_delay_timer = millis() + THINGSPEAK_READ_TEMPORARY_DELAY_TIMEOUT*1000;
              SendStatus(event->Source, F("\nThingspeak Read Delay updated....timeout in 5mins"));
            }
            else
            {
              SendStatus(event->Source, F("\nInvalid input range: Valid values between 5 and 30s"));
            }
          }
        }
//        if(other commands)...
        break;
      }

    case PLUGIN_INIT:
      {
        Serial.println("PLUGIN_INIT Executed");
        thingspeakRead_default_delay = Settings.TaskDeviceTimer[event->TaskIndex];//THINGSPEAK_READ_DEFAULT_DELAY;
        Serial.print("Plugin delay set to ");
        Serial.println(thingspeakRead_default_delay);
        thingspeakRead_current_delay = thingspeakRead_default_delay;
        thingspeakRead_timer = millis() + thingspeakRead_current_delay*1000 + 40000;
        thingspeakRead_temporary_delay_active = false;
        thingspeakRead_prev_cmd = "";
        
        success = true;
        break;
      }

    case PLUGIN_ONCE_A_SECOND:
      {
        if(Protocol[getProtocolIndex(Settings.Protocol)].Number != CPLUGIN_ID_004){ //Check if Thingspeak protocol is being used
          return success;
        }
        success = true;
        
        if(millis() > thingspeakRead_timer){
          Serial.print(".");
          success = thingspeak_read_execute_command();
        }

        if(thingspeakRead_temporary_delay_active){
          if(millis() > thingspeakRead_temporary_delay_timer){
            thingspeakRead_current_delay = thingspeakRead_default_delay;
            thingspeakRead_temporary_delay_active = false;
          }
        }

        thingspeak_publish();
        break;
      }
  }
  return success;
}

boolean thingspeak_read_execute_command()
{
  char log[80];
  thingspeakRead_timer = millis() + thingspeakRead_current_delay*1000;

  char host[20];
  sprintf_P(host, PSTR("%u.%u.%u.%u"), Settings.Controller_IP[0], Settings.Controller_IP[1], Settings.Controller_IP[2], Settings.Controller_IP[3]);
  
  sprintf_P(log, PSTR("%s%s using port %u"), "HTTP : connecting to ", host,Settings.ControllerPort);
  addLog(LOG_LEVEL_DEBUG, log);

  WiFiClient client;
  if (!client.connect(host, Settings.ControllerPort))
  {
    connectionFailures++;
    strcpy_P(log, PSTR("HTTP : connection failed"));
    addLog(LOG_LEVEL_ERROR, log);
    return false;
  }
  statusLED(true);
  if (connectionFailures)
    connectionFailures--;

  String hostName = F("api.thingspeak.com"); // PM_CZ: HTTP requests must contain host headers.
  if (Settings.UseDNS)
    hostName = Settings.ControllerHostName;

  String postStr = F("GET /channels/");
  postStr += SecuritySettings.ControllerUser; // used for Channel ID
  postStr += F("/status/last.txt?api_key=");
  postStr += SecuritySettings.ControllerPassword; // used for API Key
  postStr += F(" HTTP/1.1\r\n");
  postStr += F("Host: ");
  postStr += hostName;
  postStr += F("\r\n");
  postStr += F("Connection: close\r\n\r\n");

  // This will send the request to the server
  client.print(postStr);

  unsigned long timer = millis() + 200;
  while (!client.available() && millis() < timer)
    delay(1);

  // Read all the lines of the reply from server and print them to Serial
  if(client.available()){
    String res = client.readString();
    if (res.substring(0, 15) == "HTTP/1.1 200 OK")
    {
      strcpy_P(log, PSTR("HTTP : Success!"));
      addLog(LOG_LEVEL_DEBUG, log);
    }

    res = res.substring(res.indexOf("\r\n\r\n"));
    res.trim();
    res = res.substring(res.indexOf("\r\n"));
    res.trim();
    res = res.substring(0,res.indexOf("\r\n"));

    if(res.length()>3 && !res.equalsIgnoreCase(thingspeakRead_prev_cmd) && !res.equalsIgnoreCase(THINGSPEAK_ACK)){
      struct EventStruct TempEvent;
      Serial.print("CMD  : ");
      Serial.println(res);
      parseCommandString(&TempEvent, res);
      PluginCall(PLUGIN_WRITE, &TempEvent, res);
      thingspeak_ack = true;
      thingspeak_timer = millis() + THINGSPEAK_DELAY*1000;
    }
    thingspeakRead_prev_cmd = res;
  }
  strcpy_P(log, PSTR("HTTP : closing connection"));
  addLog(LOG_LEVEL_DEBUG, log);

  client.flush();
  client.stop();

  return true;
}

