#ifndef _LCDCONTROL_H_
#define _LCDCONTROL_H_

void fillLine(char line[17]);

void lcdShowURL(int lcdHandle);

void lcdShowIP(int lcdHandle);

void lcdShowText(int lcdHandle, char line1[17], char line2[17]);

#endif
