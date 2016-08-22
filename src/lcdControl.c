#include <unistd.h>
#include <string.h>
#include <lcd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>

#include "lcdControl.h"

void fillLine(char line[17])
{
	size_t length = strlen(line);
	for(int position=length; position < 16; position++)
	{
		line[position] = ' ';
	}
	line[16] = '\0';
}
//------------------------------------------------------------------------------

void lcdShowURL(int lcdHandle)
{
	char line1[17] = "RabbitMax";
	fillLine(line1);
	lcdPosition(lcdHandle, 0, 0);
	lcdPuts(lcdHandle, line1);
	char line2[17] = "rabbitmax.com";
	fillLine(line2);
	lcdPosition(lcdHandle, 0, 1);
	lcdPuts(lcdHandle, line2);
}
//------------------------------------------------------------------------------

void lcdShowIP(int lcdHandle)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);
	lcdPosition(lcdHandle, 0, 0);
	char line1[17] = "eth0 IPv4";
	fillLine(line1);
	lcdPuts(lcdHandle, line1);
	lcdPosition (lcdHandle, 0, 1);
	char line2[17];
	strncpy(line2, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr), sizeof line2 - 1);
	line2[16] = '\0';
	fillLine(line2);
	lcdPuts(lcdHandle, line2);
}
//------------------------------------------------------------------------------

void lcdShowText(int lcdHandle, char line1[17], char line2[17])
{
	lcdPosition(lcdHandle, 0, 0);
	fillLine(line1);
	lcdPuts(lcdHandle, line1);
	lcdPosition (lcdHandle, 0, 1);
	fillLine(line2);
	lcdPuts(lcdHandle, line2);
}
//------------------------------------------------------------------------------
