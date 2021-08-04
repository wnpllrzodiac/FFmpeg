float strength = 1.0;

const float PI = 3.141592653589793;

float Linear_ease(in float begin, in float change, in float duration, in float time) {
    return change * time / duration + begin;
}

vec2 lm_cubic_bezier(vec2 p0, vec2 p1, vec2 p2, vec2 p3,float t)
{
    float t_inv = 1.0-t;
    float t_inv_2 = pow(t_inv,2.0);
    float t_inv_3 = pow(t_inv,3.0);
    float t_2 = pow(t,2.0);
    float t_3 = pow(t,3.0);
    vec2 p = p0*t_inv_3+3.0*p1*t*t_inv_2+3.0*p2*t_2*t_inv+p3*t_3;
    return p;
}

float Exponential_easeInOut(in float begin, in float change, in float duration, in float time) {
    if (time == 0.0)
        return begin;
    else if (time == duration)
        return begin + change;
    time = time / (duration / 2.0);
    if (time < 1.0)
        return change / 2.0 * pow(2.0, 10.0 * (time - 1.0)) + begin;
    return change / 2.0 * (-pow(2.0, -10.0 * (time - 1.0)) + 2.0) + begin;
}

float Sinusoidal_easeInOut(in float begin, in float change, in float duration, in float time) {
    return -change / 2.0 * (cos(PI * time / duration) - 1.0) + begin;
}

/* random number between 0 and 1 */
float random(in vec3 scale, in float seed) {
    /* use the fragment position for randomness */
    return fract(sin(dot(gl_FragCoord.xyz + seed, scale)) * 43758.5453 + seed);
}

vec3 crossFade(in vec2 uv, in float dissolve) {
    return mix(texture2D(from, uv).rgb, texture2D(to, uv).rgb, dissolve);
}

vec4 transition(vec2 uv) {
    // Linear interpolate center across center half of the image
    vec2 center = vec2(0.5, 0.5);
    //float dissolve = Exponential_easeInOut(0.0, 1.0, 1.0, progress);

    // Mirrored sinusoidal loop. 0->strength then strength->0
    float t_progress = progress*2.0;
    if(progress>.5)
    {
        t_progress = (1.0-progress)*2.0;
    }
    vec2 p0 = vec2(0.0);
    vec2 p3 = vec2(1.0);
    vec2 p1 = vec2(0.11, 0);
    vec2 p2 = vec2(0.5, 0);
    t_progress = lm_cubic_bezier(p0,p1,p2,p3,t_progress).y;
    float strength = t_progress * .4;
    float strength1 = t_progress * .2;
    vec3 color = vec3(0.0);
    float total = 0.0;
    vec2 toCenter = center - uv;

    /* randomize the lookup values to hide the fixed number of samples */
    float offset = random(vec3(12.9898, 78.233, 151.7182), 0.0);
    for (float t = 0.0; t <= 40.0; t++) {
        float percent = (t + offset) / 40.0;
        float weight = 4.0 * (percent - percent * percent);
        if (progress < 0.5){
            // color += texture2D(from, uv +
            //  log2(length(toCenter) + .001) * toCenter * percent * strength).rgb * weight;
            color += texture2D(from, uv -
             toCenter * percent * strength).rgb * weight;
        }
        else
            color += texture2D(from, uv + toCenter * percent * strength1).rgb * weight;
        total += weight;
    }
    return vec4(color / total, texture2D(from, uv).a);
}
