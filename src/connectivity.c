#include <unistd.h>

#include "connectivity.h"

/**
 * Callback for delivered MQTT message
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
	printf("\nERROR: Connection lost.\n");
	// Try to reconnect
	while (MQTTCLIENT_SUCCESS != mqttConnect())
	{
		printf("Trying to reconnect in 10 seconds...\n");
		sleep(10);
	}
	// Register again callbacks
	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);
	printf("Successfully reconnected to MQTT broker\n");
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
 * Gracefully disconnect from the MQTT broker
 *
 */
void mqttDisconnect()
{
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
}
//------------------------------------------------------------------------------

/**
 * Connect the MQTT broker
 *
 * @return MQTTCLIENT_SUCCESS if the client successfully connects to the server; Positive value on error
 */
int mqttConnect()
{
	// Free the memory allocated to the MQTT client
	MQTTClient_destroy(&client);

	// Try to establish connection to MQTT broker
        MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

        MQTTClient_create(&client, config.address, config.clientId,
        MQTTCLIENT_PERSISTENCE_NONE, NULL);
        conn_opts.keepAliveInterval = 20;
        conn_opts.cleansession = 1;

        return MQTTClient_connect(client, &conn_opts);
}
//------------------------------------------------------------------------------
