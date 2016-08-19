#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <MQTTClient.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>

#include "BMP180.h"
#include "HTU21D.h"
#include "BH1750.h"
#include "json.h"

#define ADDRESS		"tcp://iot.eclipse.org:1883"
#define CLIENTID	"RabbitMaxClient"
#define TIMEOUT		10000L

#define TOPICTEMPERATURE "sensors/temperature"
#define TOPICPRESSURE "sensors/pressure"
#define TOPICTEMPERATURE1 "sensors/temperature1"
#define TOPICHUMIDITY "sensors/humidity"
#define TOPICLIGHT "sensors/light"

volatile MQTTClient_deliveryToken deliveredtoken;

char* machineId;

MQTTClient client;
MQTTClient_deliveryToken token;

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
	// MQTT topic's 1st level is the machine ID
	char* mqttTopic;
	// Make space for the machine id, slash and the topic
	mqttTopic = malloc(strlen(machineId) + 1 + strlen(topic));
	strcpy(mqttTopic, machineId);
	strcat(mqttTopic, "/");
	strcat(mqttTopic, topic);

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
	char* messagePayload = json_stringify(json_decode(json), "\t");
	publish(topic, messagePayload, 1, 1);
	free(messagePayload);
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
void closeMqttClient()
{
	// Gracefully disconnect from the MQTT broker
        MQTTClient_disconnect(client, 10000);
        MQTTClient_destroy(&client);

	printf("\nShutting down the MQTT client...\n");

	exit(0);
}
//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	// Handle Ctrl-C
	signal(SIGINT, closeMqttClient);

	if (0 > readMachineId())
	{
		printf("Unable to retrieve unique machine ID.\n");
		exit(EXIT_FAILURE);
	}
	printf("Machine ID: %s\n", machineId);

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

	wiringPiSetup();
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

	// Temperature1 before and after
	double temperature1 = 0;
	double temperature1Before = 0;

	double humidity = 0;
	double humidityBefore = 0;

	//Lux before and after
	int lux = 0;
	int luxBefore = 0;
	
	deliveredtoken = 0;

	while(1)
	{
		// TODO: BMP180 temperature

		// TODO: BMP180 baromentric pressure

		// HTU21D temperature
		if ( (0 == getTemperature(sensorHumidity, &temperature1)) &&
			(0.5 <= delta(temperature1Before, temperature1)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"temperature\": %.1f }", temperature1);
			publishSensorData(TOPICTEMPERATURE1, messageJson);
			temperature1Before = temperature1;
		}

		// HTU21D humidity
		if ( (0 == getHumidity(sensorHumidity, &humidity) ) &&
			(1 < delta(humidityBefore, humidity)) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"humidity\": %.0f }", humidity);
			publishSensorData(TOPICHUMIDITY, messageJson);
			humidityBefore = humidity;
		}

		// BH1750 light
		lux = getLux(sensorLight);
		if ( (0 <= lux) && (lux != luxBefore) )
		{
			char messageJson[100];
			sprintf(messageJson, "{ \"light\": %d }", lux);
			publishSensorData(TOPICLIGHT, messageJson);
			luxBefore = lux;
		}

		sleep(1);
	}
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
	return 0;
}
