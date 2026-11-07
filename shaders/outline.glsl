// Outline: dark lines along the edges in the picture, like a cartoon.
//
// A post-processing shader only sees the finished picture, not what is in
// it: it can't tell a character from the track or the menus, so every clear
// edge gets its line. Soft changes in colour (shading, most textures) are
// left alone.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 9 samples a pixel.

// How sharp a change of colour has to be to get a line: from no line at
// EDGE_LOW to a full one at EDGE_HIGH.
const float EDGE_LOW = 0.12;
const float EDGE_HIGH = 0.32;
// How dark a full line is, and how much of the colour under it stays.
const float STRENGTH = 0.85;
const float INK_TINT = 0.2;
// The line goes on the darker side of an edge, so thin bright things (text,
// white lines) keep their width; the brighter side gets this much of it.
const float BRIGHT_SIDE = 0.15;
// How far apart the samples are, in pixels: the lines' thickness.
const float REACH = 1.5;

float2 PicturePosition()
{
  return (GetCoordinates() - src_rect.xy) / src_rect.zw;
}

float3 Picture(float2 position)
{
  float2 margin = 0.5 * GetInvResolution();
  float2 at = clamp(src_rect.xy + position * src_rect.zw, src_rect.xy + margin,
                    src_rect.xy + src_rect.zw - margin);
  return SampleLocation(at).rgb;
}

float Luma(float3 colour)
{
  return dot(colour, float3(0.299, 0.587, 0.114));
}

void main()
{
  float2 position = PicturePosition();
  // A pixel of the game's picture, or of the window when that is coarser,
  // so the lines are about as thick at any internal resolution.
  float2 pixel = REACH * max(1.0 / (src_rect.zw * GetResolution()), GetInvTargetResolution());

  // Sobel: how fast each colour changes across and down, from the eight
  // pixels around this one.
  float3 top_left = Picture(position + float2(-pixel.x, -pixel.y));
  float3 top = Picture(position + float2(0.0, -pixel.y));
  float3 top_right = Picture(position + float2(pixel.x, -pixel.y));
  float3 left = Picture(position + float2(-pixel.x, 0.0));
  float3 middle = Picture(position);
  float3 right = Picture(position + float2(pixel.x, 0.0));
  float3 bottom_left = Picture(position + float2(-pixel.x, pixel.y));
  float3 bottom = Picture(position + float2(0.0, pixel.y));
  float3 bottom_right = Picture(position + float2(pixel.x, pixel.y));
  float3 across = (top_right + 2.0 * right + bottom_right) - (top_left + 2.0 * left + bottom_left);
  float3 down = (bottom_left + 2.0 * bottom + bottom_right) - (top_left + 2.0 * top + top_right);
  // Over the three colours, so an edge between two colours of the same
  // brightness counts too. A full black-to-white edge is 1.
  float change = sqrt((dot(across, across) + dot(down, down)) / 3.0) * 0.25;

  // Which side of the edge this pixel is on: darker than around it, or not.
  float around = (Luma(top_left) + Luma(top) + Luma(top_right) + Luma(left) + Luma(right) +
                  Luma(bottom_left) + Luma(bottom) + Luma(bottom_right)) / 8.0;
  float dark_side = mix(BRIGHT_SIDE, 1.0, smoothstep(-0.02, 0.04, around - Luma(middle)));

  float line = smoothstep(EDGE_LOW, EDGE_HIGH, change) * dark_side * STRENGTH;
  // The line is the colour under it, much darker: ink that follows the
  // picture rather than a flat black.
  SetOutput(float4(mix(middle, middle * INK_TINT, line), 1.0));
}
