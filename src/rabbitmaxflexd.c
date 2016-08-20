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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "BMP180.h"
#include "HTU21D.h"
#include "BH1750.h"
#include "json.h"
#include "lcdControl.h"

#define ADDRESS		"tcp://iot.eclipse.org:1883"
#define CLIENTID	"RabbitMaxClient"
#define TIMEOUT		10000L

#define TOPICTEMPERATURE "sensors/temperature"
#define TOPICPRESSURE "sensors/pressure"
#define TOPICTEMPERATURE1 "sensors/temperature1"
#define TOPICHUMIDITY "sensors/humidity"
#define TOPICLIGHT "sensors/light"

volatile MQTTClient_deliveryToken deliveredtoken;

volatile int lcdHandle;

char* machineId;

MQTTClient client;
MQTTClient_deliveryToken token;

pthread_t tid[2];

struct sensors {
	double temperature;
	double humidity;
	double temperature1;
	double pressure;
	int light;
} sensors;

/**
 * Retrieves  unique machine ID of the local system that is set during installation.
 *
 * @return Upon successful completion 0 is returned. Otherwise, negative value is returned.
 */
int readMachineId()
{
	FILE* fileMachineId = fopen("/etc/machine-id", "r");
	if (NULL == fileMachineId)
	{
		return -1;
	}

	if (0 != fseek(fileMachineId, 0, SEEK_END))
	{
		return -2;
	}

	long filesize = ftell(fileMachineId);
	rewind(fileMachineId);
	machineId = malloc(filesize * (sizeof(char)));
	fread(machineId, sizeof(char), filesize, fileMachineId);
	if ( 0 != fclose(fileMachineId))
	{
		return -3;
	}

	// Remove new line from machine ID
	char *newline;
	if( (newline = strchr(machineId, '\n')) != NULL)
	{
		*newline = '\0';
	}
	return 0;
}
//------------------------------------------------------------------------------

/**
 * Retrieves unique machine ID of the local system that is set during installation.
 *
 * @param context context
 * @param dt token
 */
void delivered(void* context, MQTTClient_deliveryToken dt)
{
	printf("Message with token %d delivered.\n", dt);
	deliveredtoken = dt;
}
//------------------------------------------------------------------------------

/**
 * Callback for receiving MQTT message
 *
 * @param context context
 * @param topicName MQTT topic
 * @param topicLen lenth of the topic
 * @param message MQTT message
 *
 * @return 1
 */
int msgarrvd(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
	int i;
	char* payloadptr;

	printf("Message arrived\n");
	printf("topic: %s\n", topicName);
	printf("message: ");

	payloadptr = message->payload;
	for(i=0; i<message->payloadlen; i++)
	{
		putchar(*payloadptr++);
	}
	putchar('\n');
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}
//------------------------------------------------------------------------------

/**
 * Callback for lost connectivity
 *
 * @param context context
 * @param cause error message
 */
void connlost(void *context, char *cause)
{
	printf("\nConnection lost\n");
	printf("cause: %s\n", cause);
}
//------------------------------------------------------------------------------

/**
 * Add machine ID as prefix to the topic and publish MQTT message
 *
 * @param topic MQTT topic
 * @param message MQTT payload
 * @param qos MQTT QoS (0, 1 or 2)
 * @param retain set to 0 to disable or 1 to enable retain flag
 *
 */
void publish(char* topic, char* message, int qos, int retain)
{
	// Make space for the machine id, slash and the topic
	size_t lenMachine = strlen(machineId);
	size_t lenTopic = strlen(topic);
	char *mqttTopic = (char*) malloc(lenMachine + 3 + lenTopic);
	// MQTT topic's 1st level is the machine ID
	memcpy(mqttTopic, machineId, lenMachine);
	// Separate levels
	memcpy(mqttTopic+lenMachine, "/", 2);
	// Add next levels in MQTT topic
	memcpy(mqttTopic+lenMachine+1, topic, lenTopic+1);

	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.payload = message;
	pubmsg.payloadlen = strlen(message);
	pubmsg.qos = qos;
	pubmsg.retained = retain;
	MQTTClient_publishMessage(client, mqttTopic, &pubmsg, &token);
	printf("Publishing message on topic %s\n", mqttTopic);

	free(mqttTopic);
}
//------------------------------------------------------------------------------

/**
 * Publish data from sensors as MQTT message with QoS 1 and enabled retain.
 *
 * @param topic MQTT topic
 * @param json MQTT payload serialized as JSON
 *
 */
void publishSensorData(char* topic, char* json)
{
	JsonNode* node = json_decode(json);
	char* messagePayload = json_stringify(node, "\t");
	publish(topic, messagePayload, 1, 1);
	free(messagePayload);
	json_delete(node);
}
//------------------------------------------------------------------------------

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
	pthread_kill(tid[0], 0);

	lcdShowURL(lcdHandle);

	// Gracefully disconnect from the MQTT broker
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);

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
				char line1[16] = "Temperature";
				char line2[16];
				sprintf(line2, "%0.1fC", sensors.temperature);
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 2:
			{
				char line1[16] = "Pressure";
				char line2[16];
				sprintf(line2, "%0.2fhPa", sensors.pressure);
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 3:
			{
				char line1[16] = "Humidity";
				char line2[16];
				sprintf(line2, "%0.0f%%", sensors.humidity);
				lcdShowText(lcdHandle, line1, line2);
			}
			break;

			case 4:
			{
				char line1[16] = "Light";
				char line2[16];
				sprintf(line2, "%d Lux", sensors.light);
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

	initSensorsData(sensors);

	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

	MQTTClient_create(&client, ADDRESS, CLIENTID,
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

	if (0 != pthread_create(&(tid[0]), NULL, &controlScreen, NULL))
	{
		printf("ERROR: Unable to create thread.\n");
	}

	wiringPiSetup();
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
		if ( (0 == getTemperature(sensorTemperature, &sensors.temperature)) &&
			(0.5 <= delta(before.temperature, sensors.temperature)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"temperature\": %.1f }", sensors.temperature);
			publishSensorData(TOPICTEMPERATURE, messageJson);
			before.temperature = sensors.temperature;
		}
		// BMP180 baromentric pressure
		if ( (0 == getPressure(sensorTemperature, &sensors.pressure)) &&
			(1 <= delta(before.pressure, sensors.pressure)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"pressure\": %.0f }", sensors.pressure);
			publishSensorData(TOPICPRESSURE, messageJson);
			before.pressure = sensors.pressure;
		}

		// HTU21D temperature
		if ( (0 == getTemperature1(sensorHumidity, &sensors.temperature1)) &&
			(0.5 <= delta(before.temperature1, sensors.temperature1)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"temperature\": %.1f }", sensors.temperature1);
			publishSensorData(TOPICTEMPERATURE1, messageJson);
			before.temperature1 = sensors.temperature1;
		}

		// HTU21D humidity
		if ( (0 == getHumidity(sensorHumidity, &sensors.humidity) ) &&
			(1 < delta(before.humidity, sensors.humidity)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"humidity\": %.0f }", sensors.humidity);
			publishSensorData(TOPICHUMIDITY, messageJson);
			before.humidity = sensors.humidity;
		}

		// BH1750 light
		sensors.light = getLux(sensorLight);
		if ( (0 <= sensors.light) && (sensors.light != before.light) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"light\": %d }", sensors.light);
			publishSensorData(TOPICLIGHT, messageJson);
			before.light = sensors.light;
		}

		sleep(1);
	}
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
	return 0;
}
