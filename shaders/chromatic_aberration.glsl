// Chromatic aberration: like a camera lens, red and blue drift apart toward
// the edges of the picture; the middle stays sharp.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 3 samples a pixel.

// How far red and blue drift apart in the corners, as a fraction of the
// picture's size.
const float STRENGTH = 0.006;

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

// The picture with red pushed outward and blue pulled inward from the middle,
// more the further out: nothing in the middle, STRENGTH in the corners.
float3 SplitColours(float2 position)
{
  float2 from_middle = position - 0.5;
  float2 shift = from_middle * dot(from_middle, from_middle) * (STRENGTH / 0.35355339);
  return float3(Picture(position - shift).r, Picture(position).g, Picture(position + shift).b);
}

void main()
{
  SetOutput(float4(SplitColours(PicturePosition()), 1.0));
}
