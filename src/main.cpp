#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

constexpr uint8_t LED_PIN = 21;
constexpr uint8_t LED_COUNT = 1;

Adafruit_NeoPixel pixel(
    LED_COUNT,
    LED_PIN,
    NEO_GRB + NEO_KHZ800
);

void setup()
{
    Serial.begin(115200);

    pixel.begin();
    pixel.setBrightness(80);
    pixel.clear();
    pixel.show();

    Serial.println("Dudu WS2812 color test");
}

void loop()
{
    Serial.println("RED");
    pixel.setPixelColor(0, pixel.Color(255, 0, 0));
    pixel.show();
    delay(2000);

    Serial.println("GREEN");
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
    pixel.show();
    delay(2000);

    Serial.println("BLUE");
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
    pixel.show();
    delay(2000);

    Serial.println("OFF");
    pixel.clear();
    pixel.show();
    delay(2000);
}