For VU-meter what you need to do is to insert microphone Vcc to 3.3V, GND TO GND and data pin to A0.
<br>
If you are not using arduino, you must define the data pin yourself, it just has to be analog input.
<br>
There is also button now defined for changing effects. At the moment I do not have schematic to share for it.
<br>
Then all you need to adjust in the code is NUM_LEDS.
<br>
Few variables you might also want to play with are: 
<strong>

- const int maxBeats
- const int maxBubbles
- const int maxRipples
- const int maxTrails
</strong>

Video of my build:
https://www.youtube.com/watch?v=Box-O3KY1qI&t=
