#include <SPI.h>
#include <MemoryFree.h>
#include <EEPROM.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SoftwareSerial.h>

#define SenderNumber "887326964"
#define NumberSensor 4
#define NumberCheckSensor 4
#define IncorrectMeasure 999
#define MaxTempAlert 61
#define MaxTempThermostat 31

#pragma region EEPROM Addr

#define alertBoolAddr 2
#define alertTempAddr 3
#define TrTempBoolAddr 4
#define TrTempAddr 5
#define TrSensorAddr 6
#define KotelStateAddr 7
#define DegreeStateAddr 8
#define b1Addr 9
#define b2Addr 10

#pragma endregion

#pragma region Pins

#define TurnOffButton 15
#define TermostatPin  A0 //pin termostat relay
#define HeatPin		  A1//pin Heat relay
#define KotelPin	  A2//pin Kotel relay
#define DegreePin	  A3//pin degree relay
#define B1Pin  16
#define B2Pin  17
#define Sensor1 A4
#define Sensor2 A5
#define Sensor3 A7
#define Sensor4 A6

#pragma endregion

#pragma region State

float sensorsTemp[NumberSensor];
DallasTemperature sensor;
OneWire oneWire[NumberSensor];
DeviceAddress insideThermometer[NumberSensor];
boolean checkTempAlert = false;
byte alertTemp;
boolean trTempBool;
byte trTemp;
byte trSensor;
boolean kotelState;
boolean degreeState;
boolean heatState;
boolean b1;
boolean b2;

#pragma endregion

byte messageIndex;
String incommingMsgSerial1;
String msgSerial = "";
int incomingByteSerial;

char bufferSerial1[33];

unsigned long checkTime = 3000;


void setup()
{
	Serial.print("freeMemory start=");
	Serial.println(freeMemory());
	pinMode(TurnOffButton, OUTPUT);
	digitalWrite(TurnOffButton, HIGH);
	pinMode(KotelPin, OUTPUT);
	pinMode(TermostatPin, OUTPUT);
	pinMode(HeatPin, OUTPUT);
	pinMode(DegreePin, OUTPUT);
	pinMode(B1Pin, OUTPUT);
	pinMode(B1Pin, OUTPUT);
	LoadStateFromEEPROM();

	Serial1.begin(9600);
	Serial.begin(9600);
	delay(2000);
	TurnOnGSM();

	oneWire[0] = OneWire(Sensor1);
	oneWire[1] = OneWire(Sensor2);
	oneWire[2] = OneWire(Sensor3);
	oneWire[3] = OneWire(Sensor4);

	GSMSetup();

	Serial.print("freeMemory end setup=");
	Serial.println(freeMemory());
}

void loop()
{
	if (checkTempAlert)
	{
		delay(5000);
		GetTemps();
		for (short i = 0; i < NumberSensor; i++)
		{
			if (sensorsTemp[i] < alertTemp)
			{
				SendState("ER:", true); //Alert = true
				checkTempAlert = false;
				SaveState();
				break;
			}
		}		
	}
	if (trTempBool && kotelState)
	{			
		if (!checkTempAlert)
		{
			GetTemps();
			delay(5000);
		}
		if (sensorsTemp[trSensor] != IncorrectMeasure)
		{
			CheckThermostat();
		}
		else
		{			
			TermostatFunc(false);
			SendState("ER:4", false);
			SaveState();
			delay(1000);
		}
	}
#pragma region SerialAvailable
	if (Serial.available() > 0)
	{
		msgSerial = "";
		delay(200);
		while (Serial.available() > 0)
		{
			incomingByteSerial = Serial.read();
			msgSerial = msgSerial + char(incomingByteSerial);
		}

		CheckMessageAndSendState(msgSerial);
	}
#pragma endregion

#pragma region Serial1Available
	if (Serial1.available() > 0)
	{
		incommingMsgSerial1 = WaitAndReturnResponse();
		Serial.println(incommingMsgSerial1);
		delay(500);
		if (incommingMsgSerial1.indexOf("+CMTI: \"ME\",") > 0)
		{
			String SMS;
			messageIndex = atoi(&incommingMsgSerial1[incommingMsgSerial1.indexOf("+CMTI: \"ME\",") + 12]);
			SMS = SendCommand("at+cmgr=" + (String)itoa(messageIndex, bufferSerial1, 10), 2000, true);
			if (SMS == "ERROR")
			{
				SendSMS("ERROR:at+cmgr");
			}
			else
			{
				Serial.print("Message: "); Serial.println(SMS);
				CheckMessageAndSendState(SMS);
				SendCommand("AT+CMGD=" + (String)itoa(messageIndex, bufferSerial1, 10) + ",0", 2000, false);
			}
		}
	}
#pragma endregion
}

void GSMSetup()
{
	Serial.print("AT:"); Serial.println(SendCommand("AT", 3000, true));
	delay(3000);
	Serial.print("AT+CPMS=\"ME\":"); Serial.println(SendCommand("AT+CPMS=\"ME\"", 3000, true));
	delay(3000);
	Serial.print("AT+CNMI=2,1,0,0,0:"); Serial.println(SendCommand("AT+CNMI=2,1,0,0,0", 3000, true));
	delay(3000);
	Serial.print("AT+CMGF=1:"); Serial.println(SendCommand("AT+CMGF=1", 3000, true));
	delay(3000);
	Serial.print("AT+CMGL=\"REC UNREAD\":"); Serial.println(SendCommand("AT+CMGL=\"REC UNREAD\"", 3000, true));
	delay(3000);
}

void CheckMessageAndSendState(String message)
{
	String error = "ER:";
	if (message.indexOf(SenderNumber) >= 0)
	{
#pragma region K
		if (message.indexOf("K:on") >= 0)
		{
			KotelFunc(true);
		}
		else if (message.indexOf("K:off") >= 0)
		{
			KotelFunc(false);
			Serial.println("TurnOffHeat");
			HeatFunc(false);
		}
#pragma endregion	
#pragma region D
		if (message.indexOf("D:") >= 0)
		{
			byte degree = atoi(&message[message.indexOf("D:") + 2]);
			if (degree == 1)
			{
				DegreeFunc(true);
			}
			else if (degree == 2)
			{
				DegreeFunc(false);
			}
			else
			{
				error.concat("1,");
			}
		}
#pragma endregion	
#pragma region Tr-t				
		if (message.indexOf("Tr-t:off") >= 0)
		{
			TermostatFunc(false);
		}
		else if (message.indexOf("Tr-t:") >= 0)
		{
			int thermostatTemp = atoi(&message[message.indexOf("Tr-t:") + 5]);
			if (thermostatTemp >= 0 && thermostatTemp < MaxTempThermostat)
			{
				if (GetTempFromSensor(trSensor) != IncorrectMeasure)
				{
					TermostatFunc(true);
					trTemp = thermostatTemp;
				}
				else
				{
					error.concat("3,");
				}
			}
			else
			{
				error.concat("2,");
			}
		}
#pragma endregion	
#pragma region Tr-s
		if (message.indexOf("Tr-s:") >= 0)
		{
			int tempSensor = atoi(&message[message.indexOf("Tr-s:") + 5]);
			if (tempSensor < NumberSensor&&tempSensor >= 0)
			{
				if (GetTempFromSensor(tempSensor) != IncorrectMeasure)
				{
					trSensor = tempSensor;
				}
				else
				{
					error.concat("3,");
				}
			}
			else
			{
				error.concat("5,");
			}
		}
#pragma endregion	
#pragma region A
		if (message.indexOf("A:off") >= 0)
		{
			checkTempAlert = false;
		}
		else if (message.indexOf("A:") >= 0)
		{
			int newTempAlert = atoi(&message[message.indexOf("A:") + 2]);
			if (newTempAlert >= 0 && newTempAlert < MaxTempAlert)
			{
				alertTemp = newTempAlert;
				checkTempAlert = true;
			}
			else
			{
				error.concat("6,");
			}
		}
#pragma endregion	
#pragma region B1
		if (message.indexOf("B1:on") >= 0)
		{
			b1 = true;
			digitalWrite(B1Pin, b1);
		}
		else if (message.indexOf("B1:off") >= 0)
		{
			b1 = false;
			digitalWrite(B1Pin, b1);
		}
#pragma endregion
#pragma region B2
		if (message.indexOf("B2:on") >= 0)
		{
			b2 = true;
			digitalWrite(B2Pin, b2);
		}
		else if (message.indexOf("B2:off") >= 0)
		{
			b2 = false;
			digitalWrite(B2Pin, b2);
		}
#pragma endregion
		SaveState();
		SendState(error, false);
		delay(1000);
	}
	else
	{
		Serial.println(SendCommand(message, 500, false));//for test
	}
}

String SendCommand(String atCmd, int dly, boolean isCheckMessageOK)
{
	Serial.print("CMD:"); Serial.println(atCmd);
	String msg;	
	for (int i = 0; i<4; i++)
	{
		Serial.print("Check:"); Serial.println(i);
		Serial1.println(atCmd);
		delay(dly);
		msg = WaitAndReturnResponse();
		Serial.println(msg);
		if (!isCheckMessageOK)
		{
			break;
		}
		if (CheckMessageOK(msg))
		{
			break;
		}
		else
		{
			msg = "ERROR";
		}
	}
	return msg;
}

String WaitAndReturnResponse()
{
	String msg;
	int incomingByte;
	while (Serial1.available() > 0)
	{
		incomingByte = (Serial1.read());
		msg = msg + char(incomingByte);
	}
	delay(1000);
	return msg;
}

String  GetTemps()
{
	char s[10];
	String temps = "";	
	for (int i = 0; i < NumberSensor; i++)
	{
		temps.concat("Temp");
		temps.concat(i);
		temps.concat(":");
		float currentTemp = GetTempFromSensor(i);
		if (currentTemp != IncorrectMeasure)
		{
			sensorsTemp[i] = currentTemp;
			temps.concat(dtostrf(currentTemp, 2, 1, s));
			temps.concat(" ");
		}
		else
		{
			sensorsTemp[i] = IncorrectMeasure;
			temps.concat("ER ");
		}
	}
	return temps;
}

void CheckThermostat()
{
	if (sensorsTemp[trSensor]<trTemp - 2)
	{
		Serial.println("TurnOnHeat");
		HeatFunc(true);
	}
	else if (sensorsTemp[trSensor]>trTemp)
	{
		Serial.println("TurnOffHeat");
		HeatFunc(false);
	}
	else
	{
		Serial.println("OKHeat");
	}
}

void SendState(String error, boolean alert)
{
	String returnMsg = "";
	if (error != "ER:")
	{
		returnMsg.concat(error);
		returnMsg.concat(" ");
	}
	if (alert)
	{
		returnMsg.concat("ALERT! ");
	}
	returnMsg.concat("K:");
	if (kotelState)
	{
		returnMsg.concat("on ");
	}
	else
	{
		returnMsg.concat("off ");
	}
	returnMsg.concat("D:");
	if (degreeState)
	{
		returnMsg.concat("1");
	}
	else
	{
		returnMsg.concat("2");
	}
	returnMsg.concat(" ");
	returnMsg.concat("H:");
	if (heatState)
	{
		returnMsg.concat("on ");
	}
	else
	{
		returnMsg.concat("off ");
	}
	returnMsg.concat("Tr-t:");
	if (trTempBool)
	{
		returnMsg.concat(trTemp);
		returnMsg.concat(" ");
	}
	else
	{
		returnMsg.concat("off ");
	}

	returnMsg.concat("Tr-s:");
	returnMsg.concat(trSensor);
	returnMsg.concat(" ");
	if (checkTempAlert)
	{
		returnMsg.concat("A:");
		returnMsg.concat(alertTemp);
		returnMsg.concat(" ");

	}
	else
	{
		returnMsg.concat("A:off ");
	}
	String temps = GetTemps();
	returnMsg.concat(temps);
	returnMsg.concat("B1:");
	if (b1)
	{
		returnMsg.concat("on ");
	}
	else
	{
		returnMsg.concat("off ");
	}
	returnMsg.concat("B2:");
	if (b2)
	{
		returnMsg.concat("on ");
	}
	else
	{
		returnMsg.concat("off ");
	}
	Serial.println(returnMsg);
	SendSMS(returnMsg);

}

void SendSMS(String text)
{
	String number;
	String result;
	number.concat("AT+CMGS=\"0");
	number.concat((String)SenderNumber);
	number.concat("\"");

	Serial.print("send message: "); Serial.println(text);
	for (int i = 0; i < 4; i++)
	{
		Serial.print("Send number"); Serial.println(i);
		SendCommand(number, 3000, false);

		Serial.print("Send CMD AT+CMGS"); Serial.println(number);
		SendCommand(text, 3000, false);
		Serial1.write(26);
		delay(5000);
		result = WaitAndReturnResponse();
		if (CheckMessageOK(result))
		{
			Serial.println("Send SMS OK");
			break;
		}
	}
}

void SaveState()
{
	EEPROM.write(TrTempBoolAddr, trTempBool);
	EEPROM.write(TrSensorAddr, trSensor);
	EEPROM.write(TrTempAddr, trTemp);
	EEPROM.write(KotelStateAddr, kotelState);
	EEPROM.write(DegreeStateAddr, degreeState);
	EEPROM.write(alertBoolAddr, checkTempAlert);
	EEPROM.write(alertTempAddr, alertTemp);
	EEPROM.write(b1Addr, b1);
	EEPROM.write(b2Addr, b2);
}

void TurnOnGSM()
{
	if (SendCommand("AT", 1000, true) != "ERROR")
	{
		Serial.println("GSM is TurnOn");
		return;
	}

	digitalWrite(TurnOffButton, LOW);
	delay(2000);
	digitalWrite(TurnOffButton, HIGH);
	delay(30000);
}

void  LoadStateFromEEPROM()
{
	checkTempAlert = EEPROM.read(alertBoolAddr);
	alertTemp = EEPROM.read(alertTempAddr);
	trTempBool = EEPROM.read(TrTempBoolAddr);
	if (!trTempBool)
	{
		digitalWrite(HeatPin, LOW);
	}
	trSensor = EEPROM.read(TrSensorAddr);
	trTemp = EEPROM.read(TrTempAddr);
	kotelState = EEPROM.read(KotelStateAddr);
	digitalWrite(KotelPin, kotelState);

	degreeState = EEPROM.read(DegreeStateAddr);
	digitalWrite(DegreePin, degreeState);

	digitalWrite(TermostatPin, trTempBool);

	b1 = EEPROM.read(b1Addr);
	b2 = EEPROM.read(b2Addr);
	digitalWrite(B1Pin, b1);
	digitalWrite(B2Pin, b2);
}

boolean CheckMessageOK(String msg)
{
	if (msg.substring(msg.length() - 4) == ("OK\r\n"))
	{
		return true;
	}
	else
		return false;
}

float GetTempFromSensor(byte numberSensor)
{
	float temp;
	DallasTemperature _sensor = DallasTemperature(&oneWire[numberSensor]);
	for (int i = 0; i < NumberCheckSensor; i++)
	{
		_sensor.begin();
		_sensor.requestTemperatures(); // Send the command to get temperatures
		if (_sensor.getAddress(insideThermometer[numberSensor], 0))
		{
			temp = _sensor.getTempC(insideThermometer[numberSensor]);
			break;
		}
		else
		{
			temp = IncorrectMeasure;
		}
	}
	return temp;
}

void TermostatFunc(boolean state)
{
	trTempBool = state;
	if (state)
	{
		digitalWrite(TermostatPin, HIGH);
	}
	else
	{
		digitalWrite(TermostatPin, LOW);
		HeatFunc(true);
	}
}

void KotelFunc(boolean state)
{
	kotelState = state;
	if (state)
	{
		digitalWrite(KotelPin, HIGH);
	}
	else
	{
		digitalWrite(KotelPin, LOW);
	}
}

void DegreeFunc(boolean state)
{
	degreeState = state;
	if (state)
	{
		digitalWrite(DegreePin, HIGH);//first degree		
	}
	else
	{
		digitalWrite(DegreePin, LOW);
	}
}

void HeatFunc(boolean state)
{
	heatState = state;
	if (state)
	{
		digitalWrite(HeatPin, LOW);
	}
	else
	{
		digitalWrite(HeatPin, HIGH);
	}
}
