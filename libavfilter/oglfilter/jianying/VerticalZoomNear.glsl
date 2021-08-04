// License: MIT

float zoom_quickness = 0.5;

#define EPSILON 0.000001

vec2 zoomNear(vec2 uv, float amount){
    vec2 UV = vec2(0.0);
    if(amount < 0.5-EPSILON){       //correct critical precision
        UV = 0.5 + ((uv - 0.5)*(1.0 - amount));
    }else{
        UV = 0.5 + ((uv - 0.5)*(2.0 - amount));
    }
    return vec2(UV.x, 1.0 - UV.y);
}

vec3 blur(sampler2D Tex, vec2 uv, float iTime, float pixelStep){
    
    vec2 dir = uv - 0.5;//方向
    // dir = normalize(dir);//单位向量
    vec3 color = vec3(0.0);
    const int len = 10;
    for(int i= -len; i <= len; i++){
        vec2 blurCoord = uv + pixelStep*float(i)*dir*2.0*iTime;
        blurCoord = abs(blurCoord);
        if(blurCoord.x > 1.0){
            blurCoord.x = 2.0 - blurCoord.x;
        }
        if(blurCoord.y > 1.0){
            blurCoord.y = 2.0 - blurCoord.y;
        }
        color += texture2D(Tex, blurCoord).rgb;
    }
    color /= float(2*len+1);
    // color = texture2D(Tex, uv).rgb;
    return color;
}

float easeInOutQuint(float t) 
{ 
    return t<0.5 ? 16.0*t*t*t*t*t : 1.0+16.0*(--t)*t*t*t*t; 
}

vec4 transition(vec2 uv) {
    float pixelStep = 10.0/float(u_screenSize.x)*0.5;
    float TT = easeInOutQuint(progress);

    float nQuick = clamp(zoom_quickness, 0.0, 1.0);
    vec2 uv1 = zoomNear(uv, TT);
    
    vec4 out_color;
    if(TT <= 0.5){
        vec3 colorA = blur(from, uv1, TT, pixelStep).rgb;
        out_color = vec4(colorA, 1.0);
    }else{
        vec3 colorB = blur(to, uv1, 1.0 - TT, pixelStep).rgb;
        out_color = vec4(colorB, 1.0);
    }
    
    return out_color;
}
