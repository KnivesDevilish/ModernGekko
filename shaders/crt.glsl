// CRT: soft scanlines, a faint phosphor stripe and slightly darker corners,
// like an old television. Light: no curve and no blur.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 1 sample a pixel.

// How dark the gaps between scanlines get, and the faint red-green-blue
// stripes of a CRT's phosphors.
const float SCANLINE_DEPTH = 0.35;
const float STRIPE_DEPTH = 0.1;
// How much darker the corners are.
const float VIGNETTE = 0.25;
// Brightens the picture back up after the scanlines darken it.
const float BRIGHTNESS = 1.18;

void main()
{
  float2 position = (GetCoordinates() - src_rect.xy) / src_rect.zw;
  float3 colour = Sample().rgb;

  // Scanlines a few window pixels apart (about 300 down the picture), so
  // they stay even at any window size instead of shimmering.
  float2 window = position * GetTargetResolution();
  float spacing = max(2.0, floor(GetTargetResolution().y / 300.0 + 0.5));
  float line = 0.5 + 0.5 * cos(6.2831853 * (window.y + 0.5) / spacing);
  // Bright colours bloom over the gap, as on a real tube.
  float luma = dot(colour, float3(0.2126, 0.7152, 0.0722));
  colour *= 1.0 - SCANLINE_DEPTH * (1.0 - line) * (1.0 - 0.6 * luma);

  float stripe = mod(floor(window.x), 3.0);
  float3 phosphor = float3(stripe == 0.0 ? 1.0 : 1.0 - STRIPE_DEPTH,
                           stripe == 1.0 ? 1.0 : 1.0 - STRIPE_DEPTH,
                           stripe == 2.0 ? 1.0 : 1.0 - STRIPE_DEPTH);
  colour *= phosphor;

  float2 from_middle = position - 0.5;
  colour *= 1.0 - VIGNETTE * dot(from_middle, from_middle) * 2.0;
  SetOutput(float4(clamp(colour * BRIGHTNESS, 0.0, 1.0), 1.0));
}
