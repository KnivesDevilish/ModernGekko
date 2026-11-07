// Vivid colours: dull colours get richer (vibrance) while colours that are
// already strong, and skin tones near them, are left almost as they are, and
// a gentle curve adds a little contrast.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 1 sample a pixel.

// How much richer a grey-ish colour gets; a fully saturated one doesn't change.
const float VIBRANCE = 0.5;
// How much of the S-curve is mixed in.
const float CONTRAST = 0.15;

void main()
{
  float3 colour = Sample().rgb;
  float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
  float saturation = max(colour.r, max(colour.g, colour.b)) - min(colour.r, min(colour.g, colour.b));
  colour = mix(float3(luma, luma, luma), colour, 1.0 + VIBRANCE * (1.0 - saturation));
  colour = clamp(colour, 0.0, 1.0);
  colour = mix(colour, colour * colour * (3.0 - 2.0 * colour), CONTRAST);
  SetOutput(float4(colour, 1.0));
}
