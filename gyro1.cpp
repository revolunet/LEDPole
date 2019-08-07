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
#include <NeoPixelAnimator.h>

const uint8_t PixelCount = 240; // make sure to set this to the number of pixels in your strip
const uint8_t PixelPin = 2;     // make sure to set this to the correct pin, ignored for Esp8266
const uint8_t PixelPerRow = 16;
const uint8_t RowCount = 15;
const uint8_t AnimCount = PixelPerRow / 5 * 2 + 1; // we only need enough animations for the tail and one extra
const uint16_t PixelFadeDuration = 200;            // third of a second
// one second divide by the number of pixels = loop once a second
const uint16_t NextPixelMoveDuration = 700 / PixelPerRow; // how fast we move through the pixels

const float intensity = 0.15f;

NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
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
    uint16_t IndexPixel; // which pixel this animation is effecting
};

NeoPixelAnimator animations(AnimCount); // NeoPixel animation management object
MyAnimationState animationState[AnimCount];
uint16_t frontPixel = 0; // the front of the loop
RgbColor frontColor;     // the color at the front of the loop

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

void FadeOutAnimUpdate(const AnimationParam &param)
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

void LoopAnimUpdate(const AnimationParam &param)
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
            frontColor = HslColor(random(360) / 360.0f, 1.0f, intensity);
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

            animations.StartAnimation(indexAnim, PixelFadeDuration, FadeOutAnimUpdate);
        }
    }
}

void setup()
{
    strip.Begin();
    strip.Show();

    SetRandomSeed();

    // we use the index 0 animation to time how often we move to the next
    // pixel in the strip
    animations.StartAnimation(0, NextPixelMoveDuration, LoopAnimUpdate);
}

void loop()
{
    // this is all that is needed to keep it running
    // and avoiding using delay() is always a good thing for
    // any timing related routines
    animations.UpdateAnimations();
    strip.Show();
}
