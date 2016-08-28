#include <pthread.h>

#define CONFIGFILE 		"/etc/rabbitmaxflex.ini"

// Default configuratons:
#define ADDRESS			"tcp://iot.eclipse.org:1883"
#define CLIENTID		"RabbitMaxClient"

#define TOPICTEMPERATURE 	"sensors/temperature"
#define TOPICPRESSURE 		"sensors/pressure"
#define TOPICTEMPERATURE1 	"sensors/temperature1"
#define TOPICHUMIDITY 		"sensors/humidity"
#define TOPICLIGHT 		"sensors/light"

#define MSGNOSENSOR 		"Sensor not found"

//Pin 31 on Raspberry Pi corresponds to BCM GPIO 6 and wiringPi pin 22
#define PINBUZZER 22

pthread_t tid[2];

struct sensors {
	double temperature;
	double humidity;
	double temperature1;
	double pressure;
	int light;
	int buzzer;
} sensors, status;
