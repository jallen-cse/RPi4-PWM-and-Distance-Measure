## RPi4 <-> PERIPHERAL SETUP:
### RGB Chip:
R <-> RPi pin 32  
G <-> RPi pin 22  
B <-> RPi pin 33  
gnd <-> any RPi gnd

### HC-SR04 Chip:

trig <-> RPi pin 15  
echo <-> RPi pin 16  
vcc <-> RPi pin 2 OR 4 (any 5V)  
gnd <-> any RPi gnd

## TO BUILD PROJECT:
in shell, type "make"; executable will be named "main"

## TO EXECUTE:
in shell, type "sudo ./main"; a message indicating the program is accepting commands will be displayed

## USAGE:
- to change RGB intensities: type "RGB-intensity \<r\> \<g\> \<b\>" where each \<r\> \<g\> \<b\> are integers 0-100 representing respective intensity
- to measure distance with HC-SR04 sensor: type "distance-measure \<n\>" where \<n\> is a positive integer representing number of measurements to average across
- to exit program: type "exit".  Resources will be released and closed appropriately
