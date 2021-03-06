#include <FastLED.h>
FASTLED_USING_NAMESPACE

#if defined(FASTLED_VERSION) && (FASTLED_VERSION < 3001000)
#warning "Requires FastLED 3.1 or later; check github for latest code."
#endif

#define INDICATOR   5
#define BUTTON_PIN  7
#define DATA_PIN    2
#define DATA_PIN2   3
#define LED_TYPE    WS2812
#define COLOR_ORDER GRB
#define NUM_LEDS    72
#define MIC_MIN     0
#define MIC_MAX     50
#define NOISE       334 //For MAX4466 mic, 339 might work also
#define SAMPLES     50
#define MIN_VOL     15
#define MIC_PIN     A0
#define MIN_LEDS    14

CRGB leds[NUM_LEDS];

uint8_t hue = 0;

const int maxBeats = 10; //Min is 2 and value has to be divisible by two
const int maxBubbles = NUM_LEDS / 3; //Decrease if there is too much action going on
const int maxRipples = 6; //Min is 2 and value has to be divisible by two
const int maxTrails = 4;
const int maxBlocks = 4;

struct bubble
{
  uint8_t brightness;
  uint8_t color;
  float pos;
  float velocity;
  uint16_t life;
  uint16_t maxLife;
  int8_t maxVelocity;
  bool exist;

  bubble() {
    init();
  }
  void Move() {
    if (velocity > maxVelocity)
      velocity = maxVelocity;
    pos += velocity;
    life++;
    brightness -= 255 / maxLife;
    if (pos > NUM_LEDS - 1) {
      velocity *= -1;
      pos = NUM_LEDS - 1;
    }
    if (pos < 0) {
      velocity *= -1;
      pos = 0;
    }
    if (life > maxLife || brightness < 20)
      exist = false;
    if (!exist)
      init();
  }
  void NewColor() {
    color = hue + random(20);
    brightness = 255;
  }
  void init() {
    pos = random(0, NUM_LEDS);
    velocity = 0.5 + (random(30) * 0.01); // Increase or decrease if needed
    life = 0;
    maxLife = 90; //How many moves bubble can do
    exist = false;
    brightness = 255;
    color = hue + random(30);
    maxVelocity = 2;
  }
};
typedef struct bubble Bubble;

struct ripple
{
  uint8_t brightness;
  uint8_t color;
  uint8_t pos;
  int8_t velocity;
  uint16_t life;
  uint16_t maxLife;
  bool exist;

  ripple() {
    init(15);
  }
  void Move() {
    pos += velocity;
    life++;
    if (pos > NUM_LEDS - 1) {
      pos = NUM_LEDS - 1;
      exist = false;
    }
    if (pos < 0) {
      pos = 0;
      exist = false;
    }
    brightness -= (255 / maxLife);
    if (life > maxLife || brightness < 20) exist = false;
    if (!exist) init(15);
  }
  void init(uint8_t MaxLife) {
    pos = random(MaxLife, NUM_LEDS - MaxLife); //Avoid spawning too close to edge
    velocity = 1;
    life = 0;
    maxLife = MaxLife;
    exist = false;
    brightness = 255;
    color = hue;
  }
};
typedef struct ripple Ripple;

uint8_t blockCount = 0;

struct block
{
  uint8_t pos[NUM_LEDS / 8]; //Array of led positions in the block
  uint8_t startPoint; //Where the block will start

  block() {
    init();
    blockCount++;
  }
  void moveUp() {
    for (int i = 0; i < NUM_LEDS / 8; i++) {
      pos[i]++;
    }
  }
  void moveDown() {
    for (int i = 0; i < NUM_LEDS / 8; i++) {
      pos[i]--;
    }
  }
  void init() {
    if (blockCount > 0)
      startPoint = (NUM_LEDS / 8) * blockCount * 2;
    else
      startPoint = 0;

    for (int i = 0; i < NUM_LEDS / 8; i++) {
      pos[i] = startPoint + i;
    }
  }
};
typedef struct block Block;

Ripple beat[maxBeats];
Ripple ripple[maxRipples];
Bubble bubble[maxBubbles];
Block block[maxBlocks];

uint8_t indexP = 0; //Index of running program
uint32_t lastTime = 0;
uint16_t sampleCount = 0;
uint16_t samples[SAMPLES];
uint8_t cBrightness;
uint8_t pBrightness = 0;
uint8_t buttonState = 0;
uint8_t buttonStatePrev = 0;
uint8_t blockMoves = 0;
uint8_t block_dir;

float MAX_VOL = MIC_MAX;
float sensitivity = 0.47; //Higher value = smaller sensitivity
float gravity = 0.55; //Higher value = leds fall back down faster
float topLED = 0;

bool reversed = false;
bool changeColor = false; //Does the function randomly change color every n seconds
bool buttonStateChanged = false;

bool permission_to_move = false;
bool one_block = false;
bool two_blocks = false;
bool four_blocks = true;

void setup() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.addLeds<LED_TYPE, DATA_PIN2, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip); //Currently running two strips
  Serial.begin(115200);
  pinMode(MIC_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(INDICATOR, OUTPUT);
  //initialize samples
  for (int i = 0; i < SAMPLES; i++)
    samples[i] = MIC_MAX;
}
void loop() {
  checkButton();
  if (buttonStateChanged)
    indexP++;
  if (indexP > 9)
    indexP = 0;
  if (indexP != 9)
    digitalWrite(INDICATOR, HIGH); //Indication led is on if we are not running blank
  switch (indexP) {
    case 0: vu();
      break;
    case 1: dots();
      break;
    case 2: split();
      break;
    case 3: brush();
      break;
    case 4: beats();
      break;
    case 5: bubbles();
      break;
    case 6: ripples();
      break;
    case 7: trails();
      break;
    case 8: blocks();
      break;
    case 9: blank();
      break;
  }
  EVERY_N_SECONDS(10) {
    if (changeColor)
      hue = random(256); //Every 10 seconds pick random color
  }
}

void checkButton() {
  uint32_t currentTime = millis();
  uint16_t interval = 200; // Had to add interval as the button was reading double taps every now and then
  buttonState = digitalRead(BUTTON_PIN);
  if (buttonState != buttonStatePrev && buttonState == 1 && (uint32_t)(currentTime - lastTime) > interval){
    buttonStateChanged = true;
    lastTime = currentTime;
  }
  else
    buttonStateChanged = false;

  if (buttonState == 1) //Long press will rotate color
    hue++;

  buttonStatePrev = buttonState;
}
void vu() {
  uint16_t audio = readInput();
  uint8_t max_threshold = NUM_LEDS - 1;
  uint8_t min_threshold = 0;
  changeColor = true;
  MAX_VOL = audioMax(audio, 5);
    audio = fscale(MIC_MIN, MAX_VOL, min_threshold, max_threshold, audio, 0.5);
    if (audio < MIN_LEDS && MAX_VOL <= MIN_VOL) audio = 0; //Avoid flickering caused by small interference
    for (uint8_t i = 0; i < audio; i++) {
      leds[i] = CHSV(hue + i * 2, 255, 255);
    }
    if (audio >= topLED)
      topLED = audio;
    else
      topLED -= gravity;
    if (topLED < min_threshold)
      topLED = min_threshold;

    leds[(uint8_t)topLED] = CHSV(220, 255, 255); //Pink led as top led with gravity
    FastLED.show();
    fadeToBlackBy(leds, NUM_LEDS, 85); //Looks smoother than using FastLED.clear()
    delay(5);
  }

void dots() {
  uint16_t audio = readInput();
  uint8_t max_threshold = NUM_LEDS - 1;
  uint8_t min_threshold = 1;
  changeColor = true;
  MAX_VOL = audioMax(audio, 5);
    audio = fscale(MIC_MIN, MAX_VOL, min_threshold, max_threshold, audio, 0.5);

    if (audio >= topLED)
      topLED = audio;
    else
      topLED -= gravity;
    if (topLED < min_threshold)
      topLED = min_threshold;

    leds[(uint8_t)topLED] = CHSV(hue, 255, 255);      //Top led with gravity
    leds[(uint8_t)topLED - 1] = leds[(uint8_t)topLED];    //His friend :D

    FastLED.show();
    FastLED.clear();
    delay(5);
  }

void split() { //Vu meter starting from middle of strip and doing same effect to both directions
  uint16_t audio = readInput();
  uint8_t max_threshold = NUM_LEDS - 1;
  uint8_t min_threshold = NUM_LEDS / 2;
  changeColor = true;
  MAX_VOL = audioMax(audio, 6);
  audio = fscale(MIC_MIN, MAX_VOL, min_threshold, max_threshold, audio, 1.0);
  if (audio < MIN_LEDS / 2 + (NUM_LEDS / 2) && MAX_VOL <= MIN_VOL) audio = min_threshold; //Avoid flickering caused by small interference
  for (uint8_t i = min_threshold; i < audio; i++) {
    leds[i] = CHSV(hue + i * 6, 250, 255);
    leds[NUM_LEDS - i - 1] = leds[i]; //LEDS DRAWN DOWN
  }
  if (topLED <= audio)
    topLED = audio;
  else
    topLED -= (gravity * 0.7);
  if (topLED < min_threshold)
    topLED = min_threshold;

  leds[(uint8_t)topLED] = CHSV(220, 255, 255);         //Top led on top of strip (PINK)
  leds[NUM_LEDS - (uint8_t)topLED - 1] = leds[(uint8_t)topLED]; //On top of bottom half of strip

  FastLED.show();
  fadeToBlackBy(leds, NUM_LEDS, 85); //Looks smoother than using FastLED.clear()
  delay(5);
}
void brush() { // Swipes/brushes new color over the old one
  uint16_t audio = readInput();
  changeColor = false;
  MAX_VOL = audioMax(audio, 8);
  fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
  if (audio > MAX_VOL * sensitivity) {
    hue = random(256);
    if (hue < 127)
      reversed = true;
    else
      reversed = false;

    if (!reversed) {
      for (uint8_t i = 0; i < NUM_LEDS; i++) { //Draw one led at a time (UP)
        leds[i] = CHSV(hue, 255, 255);
        FastLED.show();
        delay(5);
      }
    }
    else {
      for (uint8_t i = NUM_LEDS - 1; i > 0; i--) { //Draw one led at a time (DOWN)
        leds[i] = CHSV(hue, 255, 255);
        FastLED.show();
        delay(5);
      }
    }
  }
  FastLED.show();
  FastLED.clear();
}
void beats() {
  uint16_t audio = readInput();
  changeColor = true;
  MAX_VOL = audioMax(audio, 5);
  EVERY_N_MILLISECONDS(30) {
    uint8_t newBeats = fscale(MIC_MIN, MAX_VOL, 2, maxBeats, audio, 0.5); //Max amount of beats to be spawned this loop
    if (newBeats % 2 != 0) //Must be divisible by two
      newBeats--;

    for (int i = 0; i < newBeats; i += 2) {
      if (audio > MAX_VOL * sensitivity * 0.55 && !beat[i].exist) {
        uint8_t beatlife = random(6, 10); //Longer life = longer length
        beat[i].init(beatlife); //initialize life
        beat[i].exist = true;
        beat[i].color += random(30);
        beat[i + 1] = beat[i]; //Everything except velocity is the same for other beats twin
        beat[i + 1].velocity *= -1; //because we want the other to go opposite direction
      }
    }
    for (uint8_t i = 0; i < maxBeats; i++) {
      if (beat[i].exist) {
        leds[beat[i].pos] = CHSV(beat[i].color, 255, beat[i].brightness);
        beat[i].Move();
      }
    }
    FastLED.show();
    fadeToBlackBy(leds, NUM_LEDS, 51);
  }
}

void bubbles() { //Spawns bubbles that move when audio peaks enough
  uint16_t audio = readInput();
  changeColor = true;
  MAX_VOL = audioMax(audio, 4);
  if (audio > MAX_VOL * sensitivity)
  {
    uint8_t randomBubble = random(maxBubbles);
    if (bubble[randomBubble].exist) {
      bubble[randomBubble].life /= 2; //Give that bubble longer life
      bubble[randomBubble].NewColor(); // And new color
    }
    else {
      bubble[randomBubble].exist = true;
      bubble[randomBubble].NewColor();
    }
  }
  for (uint8_t i = 0; i < maxBubbles; i++) {
    if (bubble[i].exist) {
      leds[(int)bubble[i].pos] = CHSV(bubble[i].color, 255, bubble[i].brightness);
      bubble[i].Move();
    }
  }
  FastLED.show();
  FastLED.clear();
  delay(3);
}

void ripples() {
  uint16_t audio = readInput();
  changeColor = false;
  MAX_VOL = audioMax(audio, 4);
  EVERY_N_MILLISECONDS(17) {
    uint8_t newRipples = fscale(MIC_MIN, MAX_VOL, 2, maxRipples, audio, 0.5); //Max amount of ripples to be spawned this loop
    if (newRipples % 2 != 0) //Must be divisible by two
      newRipples--;

    EVERY_N_MILLISECONDS(200) {
      hue++; //Slight color rotation
    }
    uint8_t b = beatsin8(30, 70, 90); //Pulsing brightness for background (pulses,min,max)
    fill_solid(leds, NUM_LEDS, CHSV(hue, 255, b)); //Delete this line if you dont like background color
    for (uint8_t i = 0; i < newRipples; i += 2) {
      if (audio > MAX_VOL * sensitivity * 0.65 && !ripple[i].exist) {
        ripple[i].init(12); //initiliaze just in case color has changed after last init
        ripple[i].exist = true;
        ripple[i + 1] = ripple[i]; //Everything except velocity is the same for other ripples twin
        ripple[i + 1].velocity *= -1; //because we want the other to go opposite direction
      }
    }
    for (uint8_t i = 0; i < maxRipples; i++) {
      if (ripple[i].exist) {
        leds[ripple[i].pos] = CHSV(ripple[i].color + 30, 180, ripple[i].brightness);
        ripple[i].Move();
      }
    }
    FastLED.show();
    FastLED.clear();
  }
}
void trails() { //Spawns trails that move
  uint16_t audio = readInput();
  changeColor = true;
  MAX_VOL = audioMax(audio, 4);
  if (audio > MAX_VOL * sensitivity)
  {
    uint8_t randomTrail = random(maxTrails);
    if (bubble[randomTrail].exist) {
      bubble[randomTrail].life /= 2; //Extend life of trail
      bubble[randomTrail].NewColor(); // And give new color
    }
    else {
      bubble[randomTrail].exist = true;
      bubble[randomTrail].maxLife = 60;
    }
  }
  for (uint8_t i = 0; i < maxTrails; i++) {
    if (bubble[i].exist) {
      leds[(int)bubble[i].pos] = CHSV(bubble[i].color, 255, bubble[i].brightness);
      bubble[i].Move();
    }
  }
  FastLED.show();
  fadeToBlackBy(leds, NUM_LEDS, 43);
  delay(3);
}

void blocks() {
  uint16_t audio = readInput();
  uint32_t currentTime = millis();
  uint16_t interval = 300; //How long to wait before next block movement can be done(milliseconds)
  uint8_t movement;
  changeColor = false;
  MAX_VOL = audioMax(audio, 3);
  if (MAX_VOL < audio && (uint32_t)(currentTime - lastTime) > interval) { //Casting done with uint to overcome millis
    permission_to_move = true;                                         //looping over
    lastTime = currentTime;
  }
  if (permission_to_move) {
    if (one_block)
      two_blocks_open();
    else if (two_blocks) {
      if (blockMoves == 0) //To not change direction of block during the transition
        block_dir = random(2); //Randomize if to close into one block or open into four
      if (block_dir == 0)
        four_blocks_open();
      else
        two_blocks_close();
    }
    else if (four_blocks) {
      four_blocks_close();
    }
    delay(10);
  }
  uint8_t bounce = beatsin8(60, 0, NUM_LEDS / 8); //Bounces all leds up and down in circular motion
  for (uint8_t j = 0; j < maxBlocks; j++) {
    for (uint8_t i = 0; i < NUM_LEDS / 8; i++) {
      leds[block[j].pos[i] + bounce] = CHSV(hue, 255, 255);
    }
  }
  FastLED.show();
  FastLED.clear();
}

void four_blocks_close() { // Four blocks close into two blocks
  for (int i = 0; i < maxBlocks; i++) {
    if (i % (maxBlocks / 2) == 0)
      block[i].moveUp();
    else
      block[i].moveDown();
  }
  blockMoves++;
  if (blockMoves > NUM_LEDS / 16) {
    two_blocks = true;
    four_blocks = false;
    permission_to_move = false;
    hue += 20;
    blockMoves = 0;
  }
}
void two_blocks_close() { // Two blocks close into one
  for (uint8_t i = 0; i < maxBlocks; i++) {
    if (i < (maxBlocks / 2))
      block[i].moveUp();
    else
      block[i].moveDown();
  }
  blockMoves++;
  if (blockMoves > NUM_LEDS / 8) {
    one_block = true;
    two_blocks = false;
    permission_to_move = false;
    hue = random(256);
    blockMoves = 0;
  }
}
void two_blocks_open() { //One block opens into two
  for (uint8_t i = 0; i < maxBlocks; i++) {
    if (i < (maxBlocks / 2))
      block[i].moveDown();
    else
      block[i].moveUp();
  }
  blockMoves++;
  if (blockMoves > NUM_LEDS / 8) {
    two_blocks = true;
    one_block = false;
    permission_to_move = false;
    blockMoves = 0;
  }
}
void four_blocks_open() { //Two blocks open into four
  for (uint8_t i = 0; i < maxBlocks; i++) {
    if (i % (maxBlocks / 2) == 0)
      block[i].moveDown();
    else
      block[i].moveUp();
  }
  blockMoves++;
  if (blockMoves > NUM_LEDS / 16) {
    two_blocks = false;
    four_blocks = true;
    permission_to_move = false;
    blockMoves = 0;
  }
}

void blank() {
  digitalWrite(INDICATOR, LOW);
  FastLED.clear();
  FastLED.show();
  delay(5);
}

int readInput() {
  int16_t audio = analogRead(MIC_PIN);
  //Serial.println(audio);
  audio -= NOISE;
  audio = abs(audio); //Turns negative value to positive
  return audio;
}
float audioMax(int audio, int multiplier) {
  float average = 0;
  samples[sampleCount] = audio; //Add one new sample value to samples
  sampleCount++;

  for (int i = 0; i < SAMPLES; i++)
    average += samples[i];  // Calculate all samples to average variable

  average /= SAMPLES;   //Divide with amount of SAMPLES to get average of all samples
  MAX_VOL = average * multiplier;   //Higher multiplier increases the sensitivity in most functions
  if (MAX_VOL < MIN_VOL) MAX_VOL = MIN_VOL;
  if (sampleCount >= SAMPLES) sampleCount = 0;

  Serial.println(MAX_VOL);
  return MAX_VOL; //Returning this might seem useless, but I believe it makes the functions more easily readable
}

float fscale(float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve) {

  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;

  // condition curve parameter
  // limit range

  if (curve > 10) curve = 10;
  if (curve < -10) curve = -10;

  curve = (curve * -.1) ; // - invert and scale - this seems more intuitive - topLEDtive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  // Check for out of range inputValues
  if (inputValue < originalMin) {
    inputValue = originalMin;
  }
  if (inputValue > originalMax) {
    inputValue = originalMax;
  }
  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin) {
    NewRange = newEnd - newBegin;
  }
  else
  {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }
  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal  =  zeroRefCurVal / OriginalRange;   // normalize to 0 - 1 float

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax ) {
    return 0;
  }
  if (invFlag == 0) {
    rangedValue =  (pow(normalizedCurVal, curve) * NewRange) + newBegin;
  }
  else     // invert the ranges
  {
    rangedValue =  newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }
  return rangedValue;
}
