// Bloom: the brightest parts of the picture (lights, the sun, white
// highlights) glow softly onto what is around them.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 25 samples a pixel, so
// it suits integrated graphics.

// How bright a colour has to be to glow, and how gradually the glow starts.
const float THRESHOLD = 0.6;
const float KNEE = 0.3;
// How far the glow reaches, as a fraction of the picture's height.
const float RADIUS = 0.03;
// How strong it is.
const float STRENGTH = 0.75;

// Where the glow is sampled around a pixel, within a disc of radius 1, and
// how much each sample counts (z). A golden-angle spiral, which covers the
// disc evenly: sample i is at 137.5 degrees times i, at sqrt((i + 0.5) / 24)
// from the middle, and counts (1 - 0.85 d^2)^2, so the glow fades out
// toward its edge. With the pixel itself (1), the weights add up to
// TOTAL_WEIGHT.
const float3 SPIRAL[24] = float3[](
    float3(0.1443, 0.0000, 0.9649), float3(-0.1843, 0.1689, 0.8966), float3(0.0282, -0.3215, 0.8308),
    float3(0.2324, 0.3031, 0.7674), float3(-0.4264, -0.0754, 0.7067), float3(0.4039, -0.2569, 0.6484),
    float3(-0.1351, 0.5026, 0.5926), float3(-0.2577, -0.4961, 0.5393), float3(0.5590, 0.2041, 0.4885),
    float3(-0.5816, 0.2401, 0.4403), float3(0.2803, -0.5991, 0.3945), float3(0.2072, 0.6605, 0.3513),
    float3(-0.6244, -0.3619, 0.3106), float3(0.7325, -0.1610, 0.2724), float3(-0.4470, 0.6359, 0.2366),
    float3(-0.1033, -0.7970, 0.2034), float3(0.6340, 0.5343, 0.1727), float3(-0.8532, 0.0353, 0.1446),
    float3(0.6223, -0.6193, 0.1189), float3(-0.0416, 0.9004, 0.0957), float3(-0.5922, -0.7096, 0.0751),
    float3(0.9380, 0.1262, 0.0569), float3(-0.7948, 0.5530, 0.0413), float3(0.2172, -0.9654, 0.0281));
const float TOTAL_WEIGHT = 10.3775;

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

// The part of a colour bright enough to glow.
float3 Bright(float3 colour)
{
  return colour * smoothstep(THRESHOLD, THRESHOLD + KNEE, Luma(colour));
}

// The glow reaching this pixel: the bright parts nearby, averaged over the
// spiral. The spiral is turned a little differently at each pixel, so a small
// light glows as a soft patch instead of a ring of copies of itself.
float3 Glow(float2 position)
{
  float2 picture_pixels = src_rect.zw * GetResolution();
  float2 reach = float2(picture_pixels.y / picture_pixels.x, 1.0) * RADIUS;
  float turn = 6.2831853 *
               fract(52.9829189 * fract(dot(gl_FragCoord.xy, float2(0.06711056, 0.00583715))));
  float c = cos(turn);
  float s = sin(turn);
  float3 sum = Bright(Picture(position));
  for (int i = 0; i < 24; ++i)
  {
    float2 offset = float2(c * SPIRAL[i].x - s * SPIRAL[i].y, s * SPIRAL[i].x + c * SPIRAL[i].y);
    sum += Bright(Picture(position + offset * reach)) * SPIRAL[i].z;
  }
  return sum / TOTAL_WEIGHT;
}

void main()
{
  float2 position = PicturePosition();
  float3 colour = Picture(position);
  float3 glow = Glow(position) * STRENGTH;
  // Screen blending: the glow lightens the picture without washing it out.
  colour = 1.0 - (1.0 - colour) * (1.0 - glow);
  SetOutput(float4(colour, 1.0));
}
