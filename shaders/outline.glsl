// Anime outline: the picture drawn like an anime. Colours are flattened
// (small changes inside an area are smoothed away, edges kept), shaded in a
// few soft steps, as cel shading does, and made a little brighter; the
// edges get clean lines of ink, a deep tone of the colour under them rather
// than a flat black.
//
// A post-processing shader only sees the finished picture, not what is in
// it: it can't tell a character from the track or the menus, so every clear
// edge gets its line.
//
// One of ModernGekko's picture effects, chosen in the in-game menu's Quick
// settings or the launcher's Settings. A Dolphin post-processing shader
// (Sys/Shaders/ModernGekko/ in a build): one pass and 9 samples a pixel.

// How sharp a change of colour has to be to get a line: from no line at
// EDGE_LOW to a full one at EDGE_HIGH.
const float EDGE_LOW = 0.1;
const float EDGE_HIGH = 0.28;
// How strong a full line is, how dark its ink (a part of the colour under
// it) and how much richer the ink's colour is.
const float LINE_STRENGTH = 0.9;
const float INK_DARKNESS = 0.22;
const float INK_SATURATION = 1.5;
// The line goes on the darker side of an edge, so thin bright things (text,
// white lines) keep their width; the brighter side gets this much of it.
const float BRIGHT_SIDE = 0.12;
// How far apart the samples are, in pixels: the lines' thickness.
const float REACH = 1.25;
// Flattening: neighbours whose colour is this close count fully, farther
// ones less and less, so edges stay sharp.
const float FLATTEN_CLOSE = 0.12;
// Cel shading: how many steps of light, how soft the step from one to the
// next is (a part of a step), and how much of it is mixed in.
const float STEPS = 4.0;
const float STEP_SOFTNESS = 0.3;
const float CEL = 0.3;
// How much richer the colours get, and how much the brightest parts go
// towards white, as anime highlights do.
const float SATURATION = 1.18;
const float HIGHLIGHT = 0.25;

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

// How much a neighbour counts in flattening this pixel's colour.
float Closeness(float3 neighbour, float3 middle)
{
  float3 difference = neighbour - middle;
  return exp(-dot(difference, difference) / (FLATTEN_CLOSE * FLATTEN_CLOSE));
}

float3 Saturated(float3 colour, float amount)
{
  float luma = Luma(colour);
  return clamp(mix(float3(luma, luma, luma), colour, amount), 0.0, 1.0);
}

void main()
{
  float2 position = PicturePosition();
  // A pixel of the game's picture, or of the window when that is coarser,
  // so the lines are about as thick at any internal resolution.
  float2 pixel = REACH * max(1.0 / (src_rect.zw * GetResolution()), GetInvTargetResolution());

  float3 top_left = Picture(position + float2(-pixel.x, -pixel.y));
  float3 top = Picture(position + float2(0.0, -pixel.y));
  float3 top_right = Picture(position + float2(pixel.x, -pixel.y));
  float3 left = Picture(position + float2(-pixel.x, 0.0));
  float3 middle = Picture(position);
  float3 right = Picture(position + float2(pixel.x, 0.0));
  float3 bottom_left = Picture(position + float2(-pixel.x, pixel.y));
  float3 bottom = Picture(position + float2(0.0, pixel.y));
  float3 bottom_right = Picture(position + float2(pixel.x, pixel.y));

  // Flat colour: the neighbours of about the same colour, averaged.
  float3 sum = middle;
  float weight = 1.0;
  float w;
  w = Closeness(top_left, middle);     sum += top_left * w;     weight += w;
  w = Closeness(top, middle);          sum += top * w;          weight += w;
  w = Closeness(top_right, middle);    sum += top_right * w;    weight += w;
  w = Closeness(left, middle);         sum += left * w;         weight += w;
  w = Closeness(right, middle);        sum += right * w;        weight += w;
  w = Closeness(bottom_left, middle);  sum += bottom_left * w;  weight += w;
  w = Closeness(bottom, middle);       sum += bottom * w;       weight += w;
  w = Closeness(bottom_right, middle); sum += bottom_right * w; weight += w;
  float3 colour = sum / weight;

  // Cel shading: the light in a few soft steps, the colour kept. Very dark
  // parts are left as they are, so shadows don't go black.
  float luma = Luma(colour);
  float level = luma * STEPS;
  float step_start = floor(level);
  float stepped = (step_start + smoothstep(0.5 - STEP_SOFTNESS, 0.5 + STEP_SOFTNESS,
                                           level - step_start)) / STEPS;
  float3 cel = clamp(colour * (stepped / max(luma, 0.001)), 0.0, 1.0);
  colour = mix(colour, cel, CEL * smoothstep(0.04, 0.14, luma));
  colour = Saturated(colour, SATURATION);
  colour = mix(colour, float3(1.0, 1.0, 1.0), HIGHLIGHT * smoothstep(0.8, 0.97, Luma(colour)));

  // Sobel: how fast each colour changes across and down. Over the three
  // colours, so an edge between two colours of the same brightness counts
  // too. A full black-to-white edge is 1.
  float3 across = (top_right + 2.0 * right + bottom_right) - (top_left + 2.0 * left + bottom_left);
  float3 down = (bottom_left + 2.0 * bottom + bottom_right) - (top_left + 2.0 * top + top_right);
  float change = sqrt((dot(across, across) + dot(down, down)) / 3.0) * 0.25;
  // Which side of the edge this pixel is on: darker than around it, or not.
  float around = (Luma(top_left) + Luma(top) + Luma(top_right) + Luma(left) + Luma(right) +
                  Luma(bottom_left) + Luma(bottom) + Luma(bottom_right)) / 8.0;
  float dark_side = mix(BRIGHT_SIDE, 1.0, smoothstep(-0.02, 0.04, around - Luma(middle)));
  float line = smoothstep(EDGE_LOW, EDGE_HIGH, change) * dark_side * LINE_STRENGTH;
  // The ink: a deep, richer tone of the colour under it.
  float3 ink = Saturated(colour, INK_SATURATION) * INK_DARKNESS;
  SetOutput(float4(mix(colour, ink, line), 1.0));
}
