uniform sampler2D extra_tex;

uniform int baseTexWidth;
uniform int baseTexHeight;
uniform vec2 fullBlendTexSize;

uniform float alphaFactor;

// 动漫火焰 动漫闪电 动漫云朵
// uniform float timer;

// normal
vec3 blendNormal(vec3 base, vec3 blend) {
    return blend;
}

vec3 blendNormal(vec3 base, vec3 blend, float opacity) {
    return (blendNormal(base, blend) * opacity + blend * (1.0 - opacity));
}

vec3 blendFunc(vec3 base, vec3 blend, float opacity,int blendMode) {
    // blendMode == 0)
    return (blendNormal(base, blend) * opacity + base * (1.0 - opacity));
}

vec2 sucaiAlign(vec2 videoUV,vec2 videoSize,vec2 sucaiSize,vec2 anchorImageCoord,float sucaiScale)
{
    vec2 videoImageCoord = videoUV * videoSize;
    vec2 sucaiUV= (videoImageCoord - anchorImageCoord)/(sucaiSize * sucaiScale) + vec2(0.5);
    return sucaiUV;
}

vec4 blendColor(sampler2D sucai, vec4 baseColor,vec2 videoSize,vec2 sucaiSize,vec2 anchorImageCoord,float sucaiScale)
{
    vec4 resultColor = baseColor;  

    vec2 uv_ = vec2(texCoord.x, 1.0 - texCoord.y);
    vec2 sucaiUV = sucaiAlign(uv_,videoSize,sucaiSize,anchorImageCoord,sucaiScale);

    vec4 fgColor = baseColor;

     if(sucaiUV.x >= 0.0 && sucaiUV.x <= 1.0 && sucaiUV.y >= 0.0 && sucaiUV.y <= 1.0 ) {
        // sucaiUV.y = 1.0 - sucaiUV.y;
        fgColor = texture2D(sucai,sucaiUV);
    } else {
        return baseColor;
    }

    fgColor = fgColor * alphaFactor;

    int newBlendMode = 0;

    vec3 color = blendFunc(baseColor.rgb, clamp(fgColor.rgb * (1.0 / fgColor.a), 0.0, 1.0), 1.0,newBlendMode);
    resultColor.rgb = baseColor.rgb * (1.0 - fgColor.a) + color.rgb * fgColor.a;  
    resultColor.a = 1.0;    
  
    return resultColor;
}

vec4 transition (vec2 uv) {
    if (progress <= 0.0)
        return getFromColor(uv);
    else if (progress >= 1.0)
        return getToColor(uv);
        
    vec2 baseTexureSize = vec2(baseTexWidth,baseTexHeight);
    vec2 fullBlendAnchor = baseTexureSize * 0.5;
    float scale = 1.0;

    //外居中对齐
    float baseAspectRatio = baseTexureSize.y/baseTexureSize.x;
    float blendAspectRatio = fullBlendTexSize.y/fullBlendTexSize.x;
    if(baseAspectRatio >= blendAspectRatio) {
        scale = baseTexureSize.y / fullBlendTexSize.y;   
    } else {
        scale = baseTexureSize.x / fullBlendTexSize.x; 
    }

    vec4 baseColor;
    if (progress < 0.5)
        baseColor = getFromColor(uv);
    else
        baseColor = getToColor(uv);
    vec4 fullblendColor = blendColor(extra_tex,baseColor,baseTexureSize,fullBlendTexSize,
        fullBlendAnchor,scale);

    return fullblendColor;
}