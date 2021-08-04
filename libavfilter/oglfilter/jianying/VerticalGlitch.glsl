// 故障

float random1d(float n) {
    return fract(sin(n) * 43758.5453);
}

float random2d(vec2 n) {
  return fract(sin(dot(n, vec2(12.9898, 4.1414))) * 43758.5453);
}

float randomRange(in vec2 seed, in float min, in float max) {
  return min + random2d(seed) * (max - min);
}

float insideRange(float v, float bottom, float top) {
  return step(bottom, v) - step(top, v);
}

float rand(vec2 co) {
  return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

vec4 transition(vec2 uv) {
  vec2 uv_ = vec2(uv.x, 1.0 - uv.y);

  // from tex
  float iTime = clamp(progress, 0.0, 1.0);
  float speed = 0.2 * iTime;
  float amount = 0.8 * iTime;

  float sTime = floor(iTime * speed * 6.0 * 24.0);
  vec3 inColFrom = texture2D(from, uv_).rgb;
  vec3 outColFrom = inColFrom;
  float maxOffset = amount / 2.0;
  vec2 uvOff;
  for (float i = 0.0; i < 10.0; i += 1.0) {
    if (i > 10.0 * amount) break;
    float sliceY = random2d(vec2(sTime + amount, 2345.0 + float(i)));
    float sliceH = random2d(vec2(sTime + amount, 9035.0 + float(i))) * 0.25;
    float hOffset = randomRange(vec2(sTime + amount, 9625.0 + float(i)),
                                -maxOffset, maxOffset);
    uvOff = uv_;
    uvOff.x += hOffset;
    vec2 uvOff = fract(uvOff);
    if (insideRange(uv_.y, sliceY, fract(sliceY + sliceH)) == 1.0) {
      outColFrom = texture2D(from, uvOff).rgb;
    }
  }
  
  // to tex
  speed = 0.2 * (1.0 - iTime);
  amount = 0.8 * (1.0 - iTime);

  sTime = floor(progress * speed * 6.0 * 24.0);
  vec3 inColTo = texture2D(to, uv_).rgb;
  vec3 outColTo = inColTo;
  for (float i = 0.0; i < 10.0; i += 1.0) {
    if (i > 10.0 * amount) break;
    float sliceY = random2d(vec2(sTime + amount, 2345.0 + float(i)));
    float sliceH = random2d(vec2(sTime + amount, 9035.0 + float(i))) * 0.25;
    float hOffset = randomRange(vec2(sTime + amount, 9625.0 + float(i)),
                                -maxOffset, maxOffset);
    uvOff = uv_;
    uvOff.x += hOffset;
    vec2 uvOff = fract(uvOff);
    if (insideRange(uv.y, sliceY, fract(sliceY + sliceH)) == 1.0) {
      outColTo = texture2D(to, uvOff).rgb;
    }
  }

  vec4 out_color;
  if(progress < 0.5){
    out_color = vec4(outColFrom, 1.0);
  } else if (progress < 1.0){
    out_color = vec4(outColTo, 1.0);
  } else {
    out_color = texture2D(to, uv_);
  }

  return out_color;
}
