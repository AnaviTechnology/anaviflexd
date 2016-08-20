#ifndef _LCDCONTROL_H_
#define _LCDCONTROL_H_

void fillLine(char line[16]);

void lcdShowURL(int lcdHandle);

void lcdShowIP(int lcdHandle);

void lcdShowText(int lcdHandle, char line1[16], char line2[16]);

#endif
