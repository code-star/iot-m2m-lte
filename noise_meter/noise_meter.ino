
#define CONSOLE_STREAM   SerialUSB

const int pinAdc = 7;

int readSound() {
  long soundValue = 0;
  for(int i=0; i<32; i++)
  {
      soundValue += analogRead(pinAdc);
  }
 
  soundValue >>= 5;
    
  CONSOLE_STREAM.println("Sound:");
  CONSOLE_STREAM.println(soundValue);
  return soundValue / 10;
}

void setup() {
  pinMode(pinAdc, INPUT);
}

void loop() {
  readSound();
  delay(5000);
}
