// Sharpen: details and edges stand out more, without the bright or dark
// halos a plain sharpen leaves around edges.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 5 samples a pixel.

// How much the difference from the neighbours is added back.
const float STRENGTH = 0.9;

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

void main()
{
  float2 position = PicturePosition();
  // One pixel of the game's picture (at the internal resolution).
  float2 pixel = 1.0 / (src_rect.zw * GetResolution());
  float3 middle = Picture(position);
  float3 up = Picture(position - float2(0.0, pixel.y));
  float3 down = Picture(position + float2(0.0, pixel.y));
  float3 left = Picture(position - float2(pixel.x, 0.0));
  float3 right = Picture(position + float2(pixel.x, 0.0));
  float3 darkest = min(middle, min(min(up, down), min(left, right)));
  float3 brightest = max(middle, max(max(up, down), max(left, right)));
  float3 sharpened = middle + (middle - (up + down + left + right) * 0.25) * STRENGTH;
  // Kept between the darkest and brightest neighbour: no halos.
  SetOutput(float4(clamp(sharpened, darkest, brightest), 1.0));
}
