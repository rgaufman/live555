#include "FileAccess.hh"

#define PIN_SD_CARD_CS 13  
#define PIN_SD_CARD_MISO 2
#define PIN_SD_CARD_MOSI 15
#define PIN_SD_CARD_CLK  14

FileDriverSD sd(PIN_SD_CARD_CS,"/sd/test/");

void setup(){
    Serial.begin(115200);

    SPI.begin(PIN_SD_CARD_CLK, PIN_SD_CARD_MISO, PIN_SD_CARD_MOSI, PIN_SD_CARD_CS);
    set555FileDriver(sd);
    FILE* file = open555File("test.mp3","r");

    if (file==nullptr){
        Serial.println("fopen failed");
        return;
    }

    Serial.println("fopen ok");
    fclose(file);
}

void loop(){}