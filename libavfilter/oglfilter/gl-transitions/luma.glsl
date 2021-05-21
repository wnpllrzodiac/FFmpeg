// Author: gre
// License: MIT

uniform sampler2D extra_tex; //luma

vec4 transition(vec2 uv) {
  return mix(
    getToColor(uv),
    getFromColor(uv),
    step(progress, texture2D(extra_tex, uv).r)
  );
}
