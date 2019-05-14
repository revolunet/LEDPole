#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelAnimator.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

// replace with your wifi credentials
const char *ssid = "Livebox-taiti";
const char *password = "xxx";

// instantiate server at port 80 (http port)
ESP8266WebServer server(80);

// Total number of pixels
const uint8_t PixelCount = 240;

// geometry for a zig-zag soldering
const uint8_t PixelPerRow = 16;
const uint8_t RowCount = 15;

// some colors
const RgbColor black = RgbColor(0, 0, 0);
const RgbColor white = RgbColor(255, 255, 255);
const RgbColor red = RgbColor(255, 0, 0);

// With esp8266, no need to specify the port - the NeoEsp8266Dma800KbpsMethod only supports the RDX0/GPIO3 pin
// https://github.com/Makuna/NeoPixelBus/wiki/ESP8266-NeoMethods
NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);

NeoPixelAnimator animations(PixelCount);

// what is stored for state is specific to the need, in this case, the colors.
// Basically what ever you need inside the animation update function
struct ColorAnimationState
{
    RgbColor StartingColor;
    RgbColor EndingColor;
};

// one entry per pixel to match the animation timing manager
ColorAnimationState colorAnimationState[PixelCount];

void SetRandomSeed()
{
    uint32_t seed;

    // random works best with a seed that can use 31 bits
    // analogRead on a unconnected pin tends toward less than four bits
    seed = analogRead(0);
    delay(1);

    for (int shifts = 3; shifts < 31; shifts += 3)
    {
        seed ^= analogRead(0) << shifts;
        delay(1);
    }

    // Serial.println(seed);
    randomSeed(seed);
}

// simple blend function
void BlendAnimUpdate(const AnimationParam &param)
{
    // this gets called for each animation on every time step
    // progress will start at 0.0 and end at 1.0
    // we use the blend function on the RgbColor to mix
    // color based on the progress given to us in the animation
    RgbColor updatedColor = RgbColor::LinearBlend(
        colorAnimationState[param.index].StartingColor,
        colorAnimationState[param.index].EndingColor,
        param.progress);
    // apply the color to the strip
    strip.SetPixelColor(param.index, updatedColor);
}


const String HTML_PAGE = "<h1>NodeMCU light</h1><a href='https://88wzy9xlnj.codesandbox.io/'>control panel</a>";

// allume la ligne `rowIndex` avec la couleur `color`
void allumeLigne(uint16_t rowIndex, RgbColor color)
{
    // de 0 à 16
    for (int count = 0; count < PixelPerRow; count += 1)
    {
        // applique la couleur au pixel
        strip.SetPixelColor((rowIndex * PixelPerRow) + count, color);
    }
}

// retourne l'index d'un pixel à partir d'un `rowIndex` et `colIndex`
int getPixelIndex(uint8_t rowIndex, uint8_t colIndex)
{
    return (rowIndex * PixelPerRow) + colIndex;
}

// allume la colonne `colIndex` avec la couleur `color`
void allumeColonne(uint8_t colIndex, RgbColor color)
{
    for (int count = 0; count < RowCount; count += 1)
    {
        strip.SetPixelColor(getPixelIndex(count, colIndex), color);
    }
}

// applique l'animation à un pixel
void FadeColorUpdate(const AnimationParam &param)
{
    float progress = NeoEase::ExponentialOut(param.progress);
    RgbColor updatedColor = RgbColor::LinearBlend(
        colorAnimationState[param.index].StartingColor,
        colorAnimationState[param.index].EndingColor,
        progress);
    strip.SetPixelColor(param.index, updatedColor);
}

// fade a single row
void fadeRow(uint8_t rowIndex, uint16_t duration, RgbColor color)
{
    for (int count = 0; count < PixelPerRow; count += 1)
    {
        uint8_t pixel = getPixelIndex(rowIndex, count);
        colorAnimationState[pixel].StartingColor = black;
        colorAnimationState[pixel].EndingColor = color;
        animations.StartAnimation(pixel, duration, FadeColorUpdate);
    }
}

// allume toutes les leds d'une couleur avec un dégradé par ligne
void colorize(RgbColor color)
{
    for (int index = 0; index < RowCount; index += 1)
    {
      RgbColor color2 = color;
      color2.Darken(index * 5);
      fadeRow(index, 300, color2);
    }
}

// anime toutes les leds vers une couleur
void fadeAll(RgbColor color, uint32_t duration=300)
{
    for (uint8_t pixel = 0; pixel < strip.PixelCount(); pixel += 1)
    {
        colorAnimationState[pixel].StartingColor = strip.GetPixelColor(pixel);
        colorAnimationState[pixel].EndingColor = color;
        animations.StartAnimation(pixel, duration, FadeColorUpdate);
    }
}

void handleRequest()
{
    if (server.hasArg("color"))
    {
        HtmlColor color = HtmlColor();
        String colorArg = '#' + server.arg("color");
        color.Parse<HtmlColorNames>(colorArg);
        Serial.println("Set color: " + colorArg);
        fadeAll(color);
    }
    else if (server.hasArg("randomcolor"))
    {
        RgbColor randomColor = HslColor(random(360) / 360.0f, 1.0f, 0.5f);
        Serial.println("Set random color");
        colorize(randomColor);
    }
    else if (server.hasArg("off"))
    {
        fadeAll(black, 500);
    }
    else if (server.hasArg("brightness"))
    {
        strip.SetBrightness((server.arg("brightness").toInt()));
    }
    else if (server.hasArg("fullsteam"))
    {
        strip.SetBrightness(255);
        fadeAll(white);
    }
    server.send(200, "text/html", HTML_PAGE);
}

void setup()
{

    SetRandomSeed();
    strip.Begin();
    strip.Show();

    WiFi.begin(ssid, password);

    // Wait WiFi
    uint32_t counter = 0;
    while (counter < 50 && WiFi.status() != WL_CONNECTED)
    {
        delay(100);
        Serial.print(".");
        counter+=1;
    }
    
    // WiFi OK
    if (counter < 50) {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      strip.SetBrightness(100);
      colorize(RgbColor(150, 90, 0));
    } else {
      Serial.println("");
      Serial.print("NOT connected to WiFi");
      Serial.println(ssid);
      strip.SetBrightness(50);
      colorize(RgbColor(255, 0, 0));
    }

    strip.Show();
    
    server.on("/", handleRequest);
    server.begin();
    Serial.println("HTTP server started");
    
}

void loop()
{
    animations.UpdateAnimations();
    server.handleClient();
    strip.Show();
}
