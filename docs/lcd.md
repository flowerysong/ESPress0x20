<https://www.waveshare.com/wiki/3.5inch_RPi_LCD_(B)>

```
3.5inch RPi LCD (B)
WaveShare
SpotPear
Rev2.0
```

Pinout:
```
             25 26
             23 24
             21 22
             19 20
             17 18
             15 16
             13 14
             11 12
              9 10
              7 8
              5 6
              3 4
              1 2

1` 	3.3V 	Power (3.3V input)
2 	5V 	Power (5V input)
3 	NC 	NC
4 	5V 	Power (5V input)
5 	NC 	NC
6 	GND 	Ground
7 	NC 	NC
8 	NC 	NC
9 	GND 	Ground
10 	NC 	NC
11 	TP_IRQ 	The touch panel is interrupted, and it is low when it is detected that the touch panel is pressed
12 	NC 	NC
13 	NC 	NC
14 	GND 	Ground
15 	NC 	NC
16 	NC 	NC
17 	3.3V 	Power (3.3V input)
18 	LCD_RS 	Command/Data Register Select
19 	LCD_SI / TP_SI 	LCD display / SPI data input of touch panel
20 	GND 	Ground
21 	TP_SO 	SPI data output of touch panel
22 	RST 	Reset
23 	LCD_SCK / TP_SCK 	SPI clock signal for LCD display / touch panel
24 	LCD_CS 	LCD chip select signal, low level selects LCD
25 	GND 	Ground
26 	TP_CS 	Touch panel chip select signal, low level selects touch panel
```

ESP32 connections:
```
1  <-> 3V3
2  <-> 5V
6  <-> GND
18 <-> P27
19 <-> P13
21 <-> P12
22 <-> EN
23 <-> P14
24 <-> P15
26 <-> P26
```
