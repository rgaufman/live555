#include "SPI.h"
#include "SD.h"
#include "FileAccess.hh"

#define PIN_SD_CARD_CS 13  
#define PIN_SD_CARD_MISO 2
#define PIN_SD_CARD_MOSI 15
#define PIN_SD_CARD_CLK  14

FileDriverFILE fd("/sd/test/");

void setup(){
    Serial.begin(115200);
    // setup vfs for SD on the ESP32
    SPI.begin(PIN_SD_CARD_CLK, PIN_SD_CARD_MISO, PIN_SD_CARD_MOSI, PIN_SD_CARD_CS);
    if (!SD.begin(PIN_SD_CARD_CS)){
        Serial.println("SD.begin failed");
        return;
    }

    // access via reguar file api
    set555FileDriver(fd);
    FILE* file = open555File("test.mp3","r");

    if (file==nullptr){
        Serial.println("fopen failed");
        return;
    }
    Serial.println("fopen ok");
    fclose(file);
}

void loop(){}