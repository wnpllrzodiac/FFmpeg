// https://www.shadertoy.com/view/XsfGRn
// https://blog.csdn.net/Kennethdroid/article/details/104536532
// https://blog.csdn.net/candycat1992/article/details/44040273

uniform vec3 u_bgcolor; // vec3(1.0,0.8,0.8)
uniform vec3 u_heartcolor; // vec3(1.0, 0.5, 0.3)
uniform float u_duration;
uniform float u_eccentricity; // background blur 0-0.5 0.25
uniform float u_blur; // heart blur 0-0.3 0.01

const float PI = 3.141592653;

void main(void) {
    //vec2 uv = vec2(texCoord.x, 1.0 - texCoord.y);
    //gl_FragColor = texture2D(tex, uv);

    // move to center
    vec2 fragCoord = vec2(texCoord.x * u_screenSize.x, texCoord.y * u_screenSize.y);
    vec2 p = (2.0*fragCoord-u_screenSize.xy)/min(u_screenSize.y,u_screenSize.x);

    // background color
    vec3 bcol = u_bgcolor * (1.0-u_eccentricity*length(p)); // 0.38

    // animate
    float tt = mod(u_time,u_duration)/u_duration;
    float ss = pow(tt,.2)*0.5 + 0.5;
    ss = 1.0 + ss*0.5*sin(tt*6.2831*3.0 + p.y*0.5)*exp(-tt*4.0);
    p *= vec2(0.5,1.5) + ss*vec2(0.5,-0.5);

    // shape
    p.y -= 0.25;
    float a = atan(p.x,p.y) / PI;
    float r = length(p);
    float h = abs(a);
    float d = (13.0*h - 22.0*h*h + 10.0*h*h*h)/(6.0-5.0*h);

    // color
    float s = 0.75 + 0.75*p.x;
    s *= 1.0-0.4*r;
    s = 0.3 + 0.7*s;
    s *= 0.5+0.5*pow( 1.0-clamp(r/d, 0.0, 1.0 ), 0.1 );
    //vec3 hcol = vec3(1.0,0.5*r,0.3)*s;
    vec3 hcol = u_heartcolor.xyz *s;

    vec3 col = mix( bcol, hcol, smoothstep( -u_blur, u_blur, d - r) ); // 0.06

    gl_FragColor = vec4(col,1.0);
}