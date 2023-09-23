// simulate camera up/down open and close effect

vec4 transition(vec2 uv) {
  vec2 p=uv.xy/vec2(1.0).xy;
  vec4 a=getFromColor(p);
  vec4 b=getToColor(p);
  //return mix(a, b, step(1.0-p.y,progress));
  if (p.y < 0.75 + 0.125 * (1.0+cos(3.1415*4.0*progress)) && p.y > 0.25 - 0.125 * (1.0+cos(3.1415*4.0*progress)))
    return a;
  else
    return b;
}
