#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelAnimator.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <Time.h>
#include <TimeLib.h>
#include <Timezone.h>

// replace with your wifi credentials
const char *ssid = "Livebox-taiti";
const char *password = "Camillou33";

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

String ip = "0.0.0.0";

// BOOT, IDLE, GYRO, VERTICAL, WAKEUP
String status = "BOOT";

// With esp8266, no need to specify the port - the NeoEsp8266Dma800KbpsMethod only supports the RDX0/GPIO3 pin
// https://github.com/Makuna/NeoPixelBus/wiki/ESP8266-NeoMethods
NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod>
    strip(PixelCount);

NeoPixelAnimator animations(PixelCount);

NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma

//WiFiUDP ntpUDP;

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
//NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);

// what is stored for state is specific to the need, in this case, the colors.
// Basically what ever you need inside the animation update function
struct ColorAnimationState
{
    RgbColor StartingColor;
    RgbColor EndingColor;
};

// one entry per pixel to match the animation timing manager
ColorAnimationState colorAnimationState[PixelCount * 2];

// ---- gyro

// fade speed
const uint16_t GyroPixelFadeDuration = 500;
// move speed
const uint16_t GyroNextPixelMoveDuration = 700 / PixelPerRow;
// brightness
//const float gyroIntensity = 0.5f;

struct GyroAnimationState
{
    RgbColor StartingColor;
    RgbColor EndingColor;
    uint16_t IndexPixel; // which pixel this animation is effecting
};

GyroAnimationState animationState[PixelPerRow / 5 * 2 + 1];
uint16_t frontPixel = 0; // the front of the loop
RgbColor frontColor;     // the color at the front of the loop

//--- end gyro

// Define NTP properties
#define NTP_OFFSET 60 * 60                // In seconds
#define NTP_INTERVAL 60 * 1000            // In miliseconds
#define NTP_ADDRESS "europe.pool.ntp.org" // change this to whatever pool is closest (see ntp.org)

// Set up the NTP UDP client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

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

// retourne l'index d'un pixel à partir d'un `rowIndex` et `colIndex`
// en inversant une ligne sur deux (assemblage zig-zag)
uint8_t getNormalizedPixelIndex(uint8_t rowIndex, uint8_t colIndex)
{
    const uint8_t offset = rowIndex % 2 == 1 ? PixelPerRow - colIndex - 1 : colIndex;
    return (rowIndex * PixelPerRow) + offset;
}

// fade a single row
void fadeRow(uint8_t rowIndex, uint16_t duration, RgbColor color)
{
    for (int count = 0; count < PixelPerRow; count += 1)
    {
        uint8_t pixel = getNormalizedPixelIndex(rowIndex, count);
        colorAnimationState[pixel].StartingColor = strip.GetPixelColor(pixel);
        colorAnimationState[pixel].EndingColor = color;
        animations.StartAnimation(pixel, duration, FadeColorUpdate);
    }
}

// allume toutes les leds d'une couleur avec un dégradé par ligne
void colorize(RgbColor color)
{
    for (int index = 0; index < RowCount; index += 1)
    {
        //RgbColor color2 = color;
        //color2.Darken(index * 5);
        fadeRow(index, 300, color);
    }
}

// anime toutes les leds vers une couleur
void fadeAll(RgbColor color, uint32_t duration = 300)
{
    for (uint8_t pixel = 0; pixel < strip.PixelCount(); pixel += 1)
    {
        colorAnimationState[pixel].StartingColor = strip.GetPixelColor(pixel);
        colorAnimationState[pixel].EndingColor = color;
        animations.StartAnimation(pixel, duration, FadeColorUpdate);
    }
}

void GyroColumnFadeOut(const AnimationParam &param)
{
    // this gets called for each animation on every time step
    // progress will start at 0.0 and end at 1.0
    // we use the blend function on the RgbColor to mix
    // color based on the progress given to us in the animation
    RgbColor updatedColor = RgbColor::LinearBlend(
        animationState[param.index].StartingColor,
        animationState[param.index].EndingColor,
        param.progress);
    // apply the color to the strip
    for (uint8_t row = 0; row < RowCount; row++)
    {
        strip.SetPixelColor(getNormalizedPixelIndex(row, animationState[param.index].IndexPixel),
                            colorGamma.Correct(updatedColor));
    }
}

void GyroLoopAnimUpdate(const AnimationParam &param)
{
    // wait for this animation to complete,
    // we are using it as a timer of sorts
    if (param.state == AnimationState_Completed)
    {
        // done, time to restart this position tracking animation/timer
        animations.RestartAnimation(param.index);

        // pick the next pixel inline to start animating
        //
        frontPixel = (frontPixel + 1) % PixelPerRow; // increment and wrap
        if (frontPixel == 0)
        {
            // we looped, lets pick a new front color
            frontColor = HslColor(random(360) / 360.0f, 1.0f, 0.5f);
        }

        uint16_t indexAnim;
        // do we have an animation available to use to animate the next front pixel?
        // if you see skipping, then either you are going to fast or need to increase
        // the number of animation channels
        if (animations.NextAvailableAnimation(&indexAnim, 1))
        {
            animationState[indexAnim].StartingColor = frontColor;
            animationState[indexAnim].EndingColor = RgbColor(0, 0, 0);
            animationState[indexAnim].IndexPixel = frontPixel;

            animations.StartAnimation(indexAnim, GyroPixelFadeDuration, GyroColumnFadeOut);
        }
    }
}

RgbColor rowColor;
uint16_t verticalRowIndex = 1;
int verticalMoveDuration = 100;
int verticalFadeDuration = 500;

void VerticalLoopAnimUpdate(const AnimationParam &param)
{
    // wait for this animation to complete,
    // we are using it as a timer of sorts
    if (param.state == AnimationState_Completed)
    {

        // done, time to restart this position tracking animation/timer
        animations.RestartAnimation(param.index);

        // pick the next pixel inline to start animating
        //

        if (verticalRowIndex == 0)
        {
            // we looped, lets pick a new front color
            rowColor = HslColor(random(360) / 360.0f, 1.0f, 0.5f);
        }

        //strip.SetPixelColor(getNormalizedPixelIndex(verticalRowIndex, 5), RgbColor(255, 0, 0));
        // if (param.index % 2 == 0)
        // {
        int previousRowIndex = ((verticalRowIndex == 0) && RowCount) || (verticalRowIndex - 1);

        //  fadeRow(previousRowIndex, verticalFadeDuration, black);
        allumeLigne(verticalRowIndex, rowColor);
        for (uint8_t i = verticalRowIndex; i >= max(0, verticalRowIndex - 5); i -= 1)
        {
            //RgbColor rowColor2 = rowColor;
            rowColor.Darken(10 * (5 - (i + 1)));
            allumeLigne(verticalRowIndex - i - 1, rowColor);
        }
        /*    allumeLigne(verticalRowIndex, rowColor.Darken());*/

        verticalRowIndex = (verticalRowIndex + 1) % RowCount; // increment and wrap

        // }
        /*
        if (animations.NextAvailableAnimation(&indexAnim, 1))
        {

            animationState[indexAnim].StartingColor = strip.GetPixelColor(previousPixel);
            animationState[indexAnim].EndingColor = RgbColor(0, 0, 0);
            animationState[indexAnim].IndexPixel = previousPixel;

            animations.StartAnimation(indexAnim, GyroPixelFadeDuration, FadeOutAnimUpdate);
        } */
        //uint16_t indexAnim;
        // do we have an animation available to use to animate the next front pixel?
        // if you see skipping, then either you are going to fast or need to increase
        // the number of animation channels
        /*
            for (int count = 0; count < RowCount; count += 1)
        {
            fadeRow(0, verticalFadeDuration, frontColor)
        }
        if (animations.NextAvailableAnimation(&indexAnim, 1))
        {
            animationState[indexAnim].StartingColor = frontColor;
            animationState[indexAnim].EndingColor = RgbColor(0, 0, 0);
            animationState[indexAnim].IndexPixel = frontPixel;

            animations.StartAnimation(indexAnim, GyroPixelFadeDuration, FadeOutAnimUpdate);
        }
        */
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
        animations.StopAnimation(0);
        animations.StopAnimation(1);
        frontPixel = 0;
        verticalRowIndex = 0;
        status = "IDLE";
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
    else if (server.hasArg("mode"))
    {
        animations.StopAnimation(0);
        animations.StopAnimation(1);
        //strip.ClearTo(black);
        frontPixel = 0;
        verticalRowIndex = 0;

        status = server.arg("mode");
        if (status == "GYRO")
        {
            animations.StartAnimation(0, GyroNextPixelMoveDuration, GyroLoopAnimUpdate);
        }
        else if (status == "VERTICAL")
        {
            animations.StartAnimation(0, verticalMoveDuration, VerticalLoopAnimUpdate);
        }
    }
    String JSON_PAGE = "{\"control\":\"https://88wzy9xlnj.codesandbox.io\", \"ip\":\"" + (String)(ip) + "\", \"status\":\"" + status + "\"}";
    server.send(200, "application/json", JSON_PAGE);
}

long int lastEvent;
long int _now = 0;

void setup()
{
    Serial.begin(115200);
    timeClient.begin();

    Serial.println("");
    Serial.print("Starting setup");
    Serial.println("");
    SetRandomSeed();
    strip.Begin();
    strip.Show();

    Serial.println("");
    Serial.print("Try to connect WiFi");
    Serial.println("");

    WiFi.begin(ssid, password);

    // Wait WiFi
    uint32_t counter = 0;
    while (counter < 100 && WiFi.status() != WL_CONNECTED)
    {
        delay(50);
        Serial.print(".");
        counter += 1;
    }

    // WiFi OK
    if (counter < 100)
    {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(ssid);
        Serial.print("IP address: ");
        ip = WiFi.localIP().toString();
        Serial.println(ip);
        Serial.println("");
        strip.SetBrightness(100);
        colorize(RgbColor(0, 150, 0));
    }
    else
    {
        Serial.println("");
        Serial.print("NOT connected to WiFi ");
        Serial.println(ssid);
        Serial.println("");
        strip.SetBrightness(10);
        colorize(RgbColor(255, 0, 0));
    }

    strip.Show();

    server.on("/", handleRequest);
    server.begin();
    Serial.println("HTTP server started");
    //currentStatus = IDLE;
    status = "IDLE";
    // timeClient.begin();
    // lastEvent = timeClient.getEpochTime();
}

// long int timeSpent()
// {
//     //  loadLastEvent();
//     // timeClient.forceUpdate();
//     _now = timeClient.getEpochTime();
//   //  int timeSpent = _now - lastEvent;
//     Serial.println("Time spent since last alarm (s): ");
//     Serial.print(timeSpent);
//     //lastEvent = timeClient.getEpochTime();
//     return timeSpent;
// }

uint lastNtpUpdate = 0;
uint ntpDelay = 60000;
uint lastElapsed = 0;

void loop()
{
    if (WiFi.status() == WL_CONNECTED) //Check WiFi connection status
    {
        unsigned long elapsed = millis();
        if (lastElapsed == 0 || elapsed - lastElapsed > (ntpDelay))
        {
            Serial.print("Updating NTP");
            timeClient.update();
            unsigned long epochTime = timeClient.getEpochTime();
            // time_t local, utc;
            //utc = epochTime;

            Serial.print(epochTime); // 29258

            lastNtpUpdate = epochTime * 1000;
            lastElapsed = elapsed;

            uint currentHour = hour(epochTime);

            Serial.print("");
            Serial.print(currentHour);
            Serial.print("");

            // if time to switch on, start anim during 1 hour
            //int Myhour = hour(epochTime);

            /*    if (status == "IDLE")
            {
                status = "WAKEUP";
                animations.StartAnimation(0, GyroNextPixelMoveDuration, GyroLoopAnimUpdate);
            }*/

            // Then convert the UTC UNIX timestamp to local time
            //TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -300}; //UTC - 5 hours - change this as needed
            //TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -360};  //UTC - 6 hours - change this as needed
            //Timezone usEast usEastern(usEDT, usEST);
            //local = usEastern.toLocal(utc);

            //1649353270
            //1577902344
        }
    }
    else // attempt to connect to wifi again if disconnected
    {
        Serial.print("Connecting wifi....");
        WiFi.begin(ssid, password);

        delay(1000);
    }
    if (status == "IDLE")
    {
        //Serial.print("IDLE");
        animations.UpdateAnimations();
    }
    else if (status == "GYRO")
    {
        //Serial.print("GYRO");
        animations.UpdateAnimations();
    }
    else if (status == "VERTICAL")
    {
        //Serial.print("VERTICAL");
        animations.UpdateAnimations();
    }

    server.handleClient();
    strip.Show();
}
