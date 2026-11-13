// Remaster: a cleaner, livelier picture that keeps the game's own look.
// Detail is sharpened where it is soft and left alone where it is already
// sharp (contrast-adaptive, as AMD's CAS does, so no halos or grain), dull
// colours get a little richer and the contrast a touch deeper. Corners and
// creases darker than what is around them get a soft shade, as ambient
// occlusion gives (worked out from the picture: an effect doesn't see the
// scene's depth). The lights glow, and a faint dither keeps skies and
// gradients from banding.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 34 samples a pixel, so
// it suits integrated graphics.

// How much soft detail is sharpened, 0 to 1.
const float SHARPNESS = 0.75;
// How much richer a grey-ish colour gets; a fully saturated one doesn't change.
const float VIBRANCE = 0.3;
// How much of the S-curve is mixed in.
const float CONTRAST = 0.1;
// The shade: how far around a pixel it looks (a fraction of the picture's
// height), how much darker than around it a pixel has to be for the shade to
// start and to be full, and how dark a full shade is.
const float SHADE_RADIUS = 0.014;
const float SHADE_LOW = 0.04;
const float SHADE_HIGH = 0.45;
const float SHADE_DEPTH = 0.32;
// The glow: how bright a colour has to be, how gradually it starts, how far
// it reaches (a fraction of the picture's height) and how strong it is.
const float GLOW_THRESHOLD = 0.6;
const float GLOW_KNEE = 0.3;
const float GLOW_RADIUS = 0.032;
const float GLOW_STRENGTH = 0.7;

// Where the shade and the glow are sampled around a pixel, within a disc of
// radius 1, and how much each sample counts (z): a golden-angle spiral
// (sample i at 137.5 degrees times i, sqrt((i + 0.5) / 12) from the middle,
// counting (1 - 0.85 d^2)^2). With the pixel itself (1), the weights add up
// to SPIRAL_TOTAL_WEIGHT.
const float3 SPIRAL[12] = float3[](
    float3(0.2041, 0.0000, 0.9304), float3(-0.2607, 0.2389, 0.7988), float3(0.0398, -0.4547, 0.6772),
    float3(0.3288, 0.4285, 0.5656), float3(-0.6031, -0.1063, 0.4641), float3(0.5710, -0.3638, 0.3726),
    float3(-0.1905, 0.7109, 0.2912), float3(-0.3650, -0.7012, 0.2197), float3(0.7909, 0.2879, 0.1583),
    float3(-0.8220, 0.3405, 0.1070), float3(0.3953, -0.8478, 0.0657), float3(0.2944, 0.9336, 0.0344));
const float SPIRAL_TOTAL_WEIGHT = 5.6850;

// Where this pixel is in the game's picture, from 0 to 1 across it. The
// picture is src_rect of the source texture (its corner and its size).
float2 PicturePosition()
{
  return (GetCoordinates() - src_rect.xy) / src_rect.zw;
}

// The picture at a point of it, never past its edge.
float3 Picture(float2 position)
{
  float2 margin = 0.5 * GetInvResolution();
  float2 at = clamp(src_rect.xy + position * src_rect.zw, src_rect.xy + margin,
                    src_rect.xy + src_rect.zw - margin);
  return SampleLocation(at).rgb;
}

float Luma(float3 colour)
{
  return dot(colour, float3(0.2126, 0.7152, 0.0722));
}

// A turn of the spiral for this pixel, different at each one, so that what
// it gathers is a smooth patch instead of a ring of copies.
float Turn(float offset)
{
  return 6.2831853 *
         fract(52.9829189 * fract(dot(gl_FragCoord.xy, float2(0.06711056, 0.00583715)) + offset));
}

// The spiral's k-th point around a pixel, `reach` across, turned by `turn`.
float2 SpiralPoint(int k, float2 reach, float turn)
{
  float c = cos(turn);
  float s = sin(turn);
  return float2(c * SPIRAL[k].x - s * SPIRAL[k].y, s * SPIRAL[k].x + c * SPIRAL[k].y) * reach;
}

// How far `radius` (a fraction of the picture's height) reaches across and
// down, in picture positions.
float2 Reach(float radius)
{
  float2 picture_pixels = src_rect.zw * GetResolution();
  return float2(picture_pixels.y / picture_pixels.x, 1.0) * radius;
}

// Contrast-adaptive sharpening over the 3x3 pixels around this one (of the
// game's picture, at the internal resolution): how much the neighbours are
// pushed away depends on how much room the darkest and brightest of them
// leave, so flat areas and hard edges stay as they are.
float3 Sharpened(float2 position)
{
  float2 pixel = 1.0 / (src_rect.zw * GetResolution());
  float3 a = Picture(position + float2(-pixel.x, -pixel.y));
  float3 b = Picture(position + float2(0.0, -pixel.y));
  float3 c = Picture(position + float2(pixel.x, -pixel.y));
  float3 d = Picture(position + float2(-pixel.x, 0.0));
  float3 e = Picture(position);
  float3 f = Picture(position + float2(pixel.x, 0.0));
  float3 g = Picture(position + float2(-pixel.x, pixel.y));
  float3 h = Picture(position + float2(0.0, pixel.y));
  float3 i = Picture(position + float2(pixel.x, pixel.y));
  float3 low = min(min(min(d, e), min(f, b)), h);
  low += min(low, min(min(a, c), min(g, i)));
  float3 high = max(max(max(d, e), max(f, b)), h);
  high += max(high, max(max(a, c), max(g, i)));
  float3 amount = sqrt(clamp(min(low, 2.0 - high) / max(high, float3(0.0001)), 0.0, 1.0));
  float3 weight = amount * (-1.0 / mix(8.0, 5.0, SHARPNESS));
  return clamp(((b + d + f + h) * weight + e) / (1.0 + 4.0 * weight), 0.0, 1.0);
}

// How much this pixel is shaded, 0 to SHADE_DEPTH: by how much it is darker
// than the picture around it, as a part of how bright that is.
float Shade(float2 position, float luma)
{
  float2 reach = Reach(SHADE_RADIUS);
  float turn = Turn(0.25);
  float around = luma;
  for (int k = 0; k < 12; ++k)
    around += Luma(Picture(position + SpiralPoint(k, reach, turn))) * SPIRAL[k].z;
  around /= SPIRAL_TOTAL_WEIGHT;
  float darker = (around - luma) / (around + 0.08);
  return SHADE_DEPTH * smoothstep(SHADE_LOW, SHADE_HIGH, darker);
}

// The part of a colour bright enough to glow.
float3 Bright(float3 colour)
{
  return colour * smoothstep(GLOW_THRESHOLD, GLOW_THRESHOLD + GLOW_KNEE, Luma(colour));
}

// The glow reaching this pixel: the bright parts nearby, averaged over the
// spiral.
float3 Glow(float2 position)
{
  float2 reach = Reach(GLOW_RADIUS);
  float turn = Turn(0.0);
  float3 sum = Bright(Picture(position));
  for (int k = 0; k < 12; ++k)
    sum += Bright(Picture(position + SpiralPoint(k, reach, turn))) * SPIRAL[k].z;
  return sum / SPIRAL_TOTAL_WEIGHT;
}

void main()
{
  float2 position = PicturePosition();
  float3 colour = Sharpened(position);
  // Richer dull colours, then a gentle S-curve.
  float luma = Luma(colour);
  float saturation = max(colour.r, max(colour.g, colour.b)) - min(colour.r, min(colour.g, colour.b));
  colour = clamp(mix(float3(luma, luma, luma), colour, 1.0 + VIBRANCE * (1.0 - saturation)), 0.0, 1.0);
  colour = mix(colour, colour * colour * (3.0 - 2.0 * colour), CONTRAST);
  // The shade in corners and creases.
  colour *= 1.0 - Shade(position, Luma(Picture(position)));
  // The glow, screen-blended: it lightens without washing out.
  float3 glow = min(Glow(position) * GLOW_STRENGTH, float3(1.0, 1.0, 1.0));
  colour = 1.0 - (1.0 - colour) * (1.0 - glow);
  // Half a step of 8-bit colour of noise, against banding.
  float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, float2(0.06711056, 0.00583715)) + 0.5));
  colour += (noise - 0.5) / 255.0;
  SetOutput(float4(clamp(colour, 0.0, 1.0), 1.0));
}
