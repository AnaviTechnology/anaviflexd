#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <MQTTClient.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include<pthread.h>
#include <lcd.h>

#include "BMP180.h"
#include "HTU21D.h"
#include "BH1750.h"
#include "json.h"
#include "lcdControl.h"
#include "machineId.h"
#include "connectivity.h"
#include "configuration.h"

#define CONFIGFILE 	"/etc/rabbitmaxflex.ini"

// Default configuratons:
#define ADDRESS		"tcp://iot.eclipse.org:1883"
#define CLIENTID	"RabbitMaxClient"

#define TOPICTEMPERATURE "sensors/temperature"
#define TOPICPRESSURE "sensors/pressure"
#define TOPICTEMPERATURE1 "sensors/temperature1"
#define TOPICHUMIDITY "sensors/humidity"
#define TOPICLIGHT "sensors/light"

#define MSGNOSENSOR "Sensor not found"

pthread_t tid;

struct sensors {
	double temperature;
	double humidity;
	double temperature1;
	double pressure;
	int light;
} sensors, status;

/**
 * Calculate delta (aka difference) between an old and new data
 *
 * @param before old data
 * @param after new data
 *
 * @return always returns non-negative delta
 */
double delta(double before, double after)
{
	double delta = before - after;
	return (0 > delta) ? delta *= -1 : delta;
}
//------------------------------------------------------------------------------

/**
 * Callback to handle Ctrl-C
 *
 */
void shutDownDaemon()
{
	pthread_kill(tid, 0);

	lcdShowURL(lcdHandle);
	mqttDisconnect();

	printf("\nShutting down the MQTT client...\n");

	exit(0);
}
//------------------------------------------------------------------------------

void initSensorsData(struct sensors data)
{
	data.temperature = 0;
	data.pressure = 0;
	data.humidity = 0;
	data.light = 0;
}
//------------------------------------------------------------------------------

void* controlScreen(void *arg)
{
	lcdHandle = lcdInit(2, 16, 4, 7, 29, 2, 3, 12, 13, 0, 0, 0, 0);
	lcdShowURL(lcdHandle);
	sleep(3);
	int counter = 0;
	while(1)
	{
		switch(counter)
		{
			case 0:
			{
				lcdShowIP(lcdHandle);
			}
			break;

			case 1:
			{
				char line1[17] = "Temperature";
				char line2[17] = MSGNOSENSOR;
				if (1 == status.temperature)
				{
					sprintf(line2, "%0.1fC", sensors.temperature);
				}
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 2:
			{
				char line1[17] = "Pressure";
				char line2[17] = MSGNOSENSOR;
				if (1 == status.pressure)
				{
					sprintf(line2, "%0.2fhPa", sensors.pressure);
				}
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 3:
			{
				char line1[17] = "Humidity";
				char line2[17] = MSGNOSENSOR;
				if (1 == status.humidity)
				{
					sprintf(line2, "%0.0f%%", sensors.humidity);
				}
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 4:
			{
				char line1[17] = "Light";
				char line2[17] = MSGNOSENSOR;
				if (1 == status.light)
				{
					sprintf(line2, "%d Lux", sensors.light);
				}
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

		}
		sleep(3);
		if (4 == counter)
		{
			counter = 0;
		}
		else
		{
			counter++;
		}
	}
	return NULL;
}
//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	// Handle Ctrl-C
	signal(SIGINT, shutDownDaemon);

	if (0 > readMachineId())
	{
		printf("Unable to retrieve unique machine ID.\n");
		exit(EXIT_FAILURE);
	}
	printf("Machine ID: %s\n", machineId);

	configuration config;
	if (0 > ini_parse(CONFIGFILE, iniConfigParser, &config))
	{
		printf("ERROR: Cannot open '%s'. Loading default configrations...\n", CONFIGFILE);
		config.address = ADDRESS;
		config.clientId =  CLIENTID;
	}
	printf("===CONFIGURATIONS===\n");
	printf("MQTT address: %s\n", config.address);
	printf("MQTT client ID: %s\n", config.clientId);
	printf("====================\n");

	initSensorsData(sensors);
	// Data structure to store status of the "plug and play" sensors
	// 0 if the sensor is not available, 1 if it is available
	initSensorsData(status);

	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

	MQTTClient_create(&client, config.address, config.clientId,
	MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

	int mqttConnect = MQTTClient_connect(client, &conn_opts);
	if (MQTTCLIENT_SUCCESS != mqttConnect)
	{
		printf("Failed to connect, return code %d\n", mqttConnect);
		exit(EXIT_FAILURE);
	}

	wiringPiSetup();

	if (0 != pthread_create(&(tid), NULL, &controlScreen, NULL))
	{
		printf("ERROR: Unable to create thread for handling LCD display.\n");
	}

	int sensorTemperature = wiringPiI2CSetup(BMP180_I2CADDR);
	if ( 0 > sensorTemperature )
	{
		fprintf(stderr, "ERROR: Unable to access RabbitMax temperature sensor: %s\n", strerror (errno));
	}
	if (0 > begin(sensorTemperature))
	{
		fprintf(stderr, "ERROR: RabbitMax temperature sensor not found.\n");
	}

	int sensorHumidity = wiringPiI2CSetup(HTU21D_I2C_ADDR);
	if ( 0 > sensorHumidity )
	{
		fprintf(stderr, "ERROR: Unable to access RabbitMax humidity sensor: %s\n", strerror (errno));
	}

	int sensorLight = wiringPiI2CSetup(BH1750_ADDR);
	if ( 0 > sensorLight)
	{
		fprintf(stderr, "ERROR: Unable to access RabbitMax light sensor: %s\n", strerror (errno));
	}

	// Store old sensor data
	struct sensors before;
	initSensorsData(before);
	
	deliveredtoken = 0;

	while(1)
	{
		// BMP180 temperature
		if (0 == getTemperature(sensorTemperature, &sensors.temperature))
		{
			status.temperature = 1;
			if (0.5 <= delta(before.temperature, sensors.temperature))
			{
				char messageJson[100];
				sprintf(messageJson, "{ \"temperature\": %.1f }", sensors.temperature);
				publishSensorData(TOPICTEMPERATURE, messageJson);
				before.temperature = sensors.temperature;
			}
		}
		else
		{
			status.temperature = 0;
		}

		// BMP180 baromentric pressure
		if (0 == getPressure(sensorTemperature, &sensors.pressure))
		{
			status.pressure = 1;
			if (1 <= delta(before.pressure, sensors.pressure))
			{
				char messageJson[100];
				sprintf(messageJson, "{ \"pressure\": %.0f }", sensors.pressure);
				publishSensorData(TOPICPRESSURE, messageJson);
				before.pressure = sensors.pressure;
			}
		}
		else
		{
			status.pressure = 0;
		}

		// HTU21D temperature
		if (0 == getTemperature1(sensorHumidity, &sensors.temperature1))
		{
			status.temperature1 = 1;
			if (0.5 <= delta(before.temperature1, sensors.temperature1))
			{
				char messageJson[100];
				sprintf(messageJson, "{ \"temperature\": %.1f }", sensors.temperature1);
				publishSensorData(TOPICTEMPERATURE1, messageJson);
				before.temperature1 = sensors.temperature1;
			}
		}
		else
		{
			status.temperature1 = 0;
		}

		// HTU21D humidity
		if (0 == getHumidity(sensorHumidity, &sensors.humidity))
		{
			status.humidity = 1;
			if (1 < delta(before.humidity, sensors.humidity))
			{
				char messageJson[100];
				sprintf(messageJson, "{ \"humidity\": %.0f }", sensors.humidity);
				publishSensorData(TOPICHUMIDITY, messageJson);
				before.humidity = sensors.humidity;
			}
		}
		else
		{
			status.humidity = 0;
		}

		// BH1750 light
		if (0 == getLux(sensorLight, &sensors.light))
		{
			status.light = 1;
			if ( (0 <= sensors.light) && (sensors.light != before.light) )
			{
				char messageJson[100];
				sprintf(messageJson, "{ \"light\": %d }", sensors.light);
				publishSensorData(TOPICLIGHT, messageJson);
				before.light = sensors.light;
			}
		}
		else
		{
			status.light = 0;
		}

		sleep(1);
	}
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
	return 0;
}
//------------------------------------------------------------------------------
