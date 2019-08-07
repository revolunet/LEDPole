// NeoPixelFunLoop
// This example will move a trail of light around a series of pixels.
// A ring formation of pixels looks best.
// The trail will have a slowly fading tail.
//
// This will demonstrate the use of the NeoPixelAnimator.
// It shows the advanced use an animation to control the modification and
// starting of other animations.
// It also shows the normal use of animating colors.
// It also demonstrates the ability to share an animation channel rather than
// hard code them to pixels.
//

#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelAnimator.h>

const uint8_t PixelCount = 240; // make sure to set this to the number of pixels in your strip
const uint8_t PixelPin = 2;     // make sure to set this to the correct pin, ignored for Esp8266
const uint8_t PixelPerRow = 16;
const uint8_t RowCount = 15;
const uint8_t AnimCount = 4;           //PixelPerRow / 5 * 2 + 1; // we only need enough animations for the tail and one extra
const uint16_t PixelFadeDuration = 50; // third of a second
// one second divide by the number of pixels = loop once a second
const uint16_t NextRowMoveDuration = 100; // how fast we move through the rows

RgbColor currentColor;

const float intensity = 0.5f;

NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma

NeoPixelBrightnessBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);

// NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
// For Esp8266, the Pin is omitted and it uses GPIO3 due to DMA hardware use.
// There are other Esp8266 alternative methods that provide more pin options, but also have
// other side effects.
// for details see wiki linked here https://github.com/Makuna/NeoPixelBus/wiki/ESP8266-NeoMethods

// what is stored for state is specific to the need, in this case, the colors and
// the pixel to animate;
// basically what ever you need inside the animation update function

struct MyAnimationState
{
    RgbColor StartingColor;
    RgbColor EndingColor;
    uint8_t RowIndex;
    //uint16_t IndexPixel; // which pixel this animation is effecting
};

NeoPixelAnimator animations(AnimCount); // NeoPixel animation management object
MyAnimationState animationState[AnimCount];
uint8_t row = 0; // the front of the loop
//RgbColor frontColor; // the color at the front of the loop

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

// retourne l'index d'un pixel à partir d'un `rowIndex` et `colIndex`
uint8_t getPixelIndex(uint8_t rowIndex, uint8_t colIndex)
{
    return (rowIndex * PixelPerRow) + colIndex;
}

// retourne l'index d'un pixel à partir d'un `rowIndex` et `colIndex`
// en inversant une ligne sur deux (assemblage zig-zag)
uint8_t getNormalizedPixelIndex(uint8_t rowIndex, uint8_t colIndex)
{
    const uint8_t offset = rowIndex % 2 == 1 ? PixelPerRow - colIndex - 1 : colIndex;
    return (rowIndex * PixelPerRow) + offset;
}

void RowFadeUpdate(const AnimationParam &param)
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
    for (uint8_t col = 0; col < PixelPerRow; col++)
    {

        strip.SetPixelColor(getNormalizedPixelIndex(animationState[param.index].RowIndex, col), colorGamma.Correct(updatedColor));
    }
    // tail
    /* const uint8_t tailSize = min(3, (int)animationState[param.index].RowIndex);
    for (uint8_t offset = 0; offset < tailSize; offset++)
    {
        updatedColor.Darken(10 * (offset + 1));
        for (uint8_t col = 0; col < PixelPerRow; col++)
        {
            strip.SetPixelColor(getNormalizedPixelIndex(animationState[param.index].RowIndex - (offset + 1), col), colorGamma.Correct(updatedColor));
        }
    }*/
}

void LoopAnimUpdate(const AnimationParam &param)
{
    /* Serial.println("");
    Serial.println("LoopAnimUpdate");
    Serial.println("");
*/
    // wait for this animation to complete,
    // we are using it as a timer of sorts
    if (param.state == AnimationState_Completed)
    {

        // done, time to restart this position tracking animation/timer
        animations.RestartAnimation(param.index);

        //Serial.println("color");
        //Serial.println(String(color));
        //Serial.println(String(color));
        // Serial.println("");

        uint16_t indexAnim;
        // do we have an animation available to use to animate the next front pixel?
        // if you see skipping, then either you are going to fast or need to increase
        // the number of animation channels

        // hide previous
        if (animations.NextAvailableAnimation(&indexAnim, 1))
        {

            // Serial.println("NextAvailableAnimation");
            animationState[indexAnim].RowIndex = row == 0 ? RowCount - 1 : row - 1;
            animationState[indexAnim].StartingColor = strip.GetPixelColor(getNormalizedPixelIndex(animationState[indexAnim].RowIndex, 0));
            animationState[indexAnim].EndingColor = RgbColor(0, 0, 0);

            animations.StartAnimation(indexAnim, PixelFadeDuration, RowFadeUpdate);
        }

        /// resetColor on each start
        if (row % RowCount == 0)
        {
            currentColor = RgbColor(HslColor(random(360) / 360.0f, 1.0f, intensity));
        }

        // show current
        if (animations.NextAvailableAnimation(&indexAnim, 1))
        {

            // Serial.println("NextAvailableAnimation");
            animationState[indexAnim].StartingColor = strip.GetPixelColor(getNormalizedPixelIndex(row, 0));
            animationState[indexAnim].EndingColor = currentColor;
            animationState[indexAnim].RowIndex = row;

            animations.StartAnimation(indexAnim, PixelFadeDuration, RowFadeUpdate);
        }

        row = (row + 1) % RowCount;
    }
}

void setup()
{
    SetRandomSeed();
    strip.Begin();
    strip.Show();

    /*
    for (uint8_t row = 0; row < RowCount; row++)
    {
        RgbColor color = HslColor(random(360) / 360.0f, 1.0f, intensity);
        strip.SetPixelColor(getNormalizedPixelIndex(row, 5), color);
    }

    //  strip.SetPixelColor(42, RgbColor(255, 0, 0));

    strip.Show();
*/
    /*  Serial.begin(115200);

    Serial.println("");
    Serial.println("Hello world");
    Serial.println("");

    
*/
    strip.SetBrightness(10);
    // we use the index 0 animation to time how often we move to the next
    // pixel in the strip
    animations.StartAnimation(0, NextRowMoveDuration, LoopAnimUpdate);
}

void loop()
{
    // this is all that is needed to keep it running
    // and avoiding using delay() is always a good thing for
    // any timing related routines
    animations.UpdateAnimations();

    strip.Show();
}
