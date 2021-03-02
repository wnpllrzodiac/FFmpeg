float seed = 2.31;

// https://panda1234lee.blog.csdn.net/article/details/105969304
 
#define PI 3.141592653589
#define PI_HALF (PI/2.)
#define clamps(x) clamp(x, 0., 1.)
 
float sigmoid(float x, float a)
{
    float b = pow(x * 2., a) / 2.;
    if (x > .5)
    {
        b = 1. - pow(2. - (x * 2.), a) / 2.;
    }
    return b;
}
 
float apow(float a, float b)
{
    return pow(abs(a), b) * sign(b);
}
 
/// @brief 三通道的 apow
vec3 pow3(vec3 a, vec3 b)
{
    return vec3(apow(a.r, b.r), apow(a.g, b.g), apow(a.b, b.b));
}
 
/// @brief 平滑混合
float smoothMix(float a, float b, float c)
{
    return mix(a, b, sigmoid(c, 2.));
}
 
/// ---------------------------------------------------------
/// 随机函数部分
float rand(float co)
{
    return fract(sin((co * 24.9898) + seed) * 43758.5453);
}
 
float rand(vec2 co)
{
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}
 
float rand(vec2 co, float shft)
{
    co += 10.;
    return smoothMix(fract(sin(dot(co.xy, vec2(12.9898 + (floor(shft) * .5), 78.233 + seed))) * 43758.5453),
                     fract(sin(dot(co.xy, vec2(12.9898 + (floor(shft + 1.) * .5), 78.233 + seed))) * 43758.5453),
                     fract(shft));
}
 
float smoothRandom(vec2 co, float shft)
{
    return smoothMix(smoothMix(rand(floor(co), shft),
                               rand(floor(co + vec2(1., 0.)), shft), fract(co.x)),
                     smoothMix(rand(floor(co + vec2(0., 1.)), shft),
                               rand(floor(co + vec2(1., 1.)), shft), fract(co.x)),
                     fract(co.y));
}
/// ---------------------------------------------------------
 
/// @brief 纹理平滑混合
vec4 textureSmoothMix(vec2 p)
{
    return mix(getFromColor(p), getToColor(p), sigmoid(progress, 10.));
}
 
vec3 color = vec3(1., 0.7, 0.6);//
float repeats = 50.0;// in {0.0, 100.0}
 
vec4 transition(vec2 p)
{
    /// @note 粒子效果
    vec3 f = vec3(0.);
    /// 粒子的个数
    for (float i = 0.; i < 20.; i++)
    {
        /// @note 大 Blob
        /// 增加一点随机性，改变每个 blob 的形状
        f += .1 +
             sin(((p.x * rand(i) * 6.0) + ///< 影响 blob 的大小
                  (progress * 8.0)) +     ///< 影响 blob 的速度
                 rand(i + 1.43)) *
             cos(((p.y * rand(i + 4.4) * 6.0) + ///< 影响 blob 的大小
                  (progress * 6.0)) +           ///< 影响 blob 的速度
                 rand(i + 2.4));
        // -------------------------------------------------------------------------
 
        /// @note 小粒子
        f += 1. - clamps(length(p -
                                vec2(smoothRandom(vec2(progress * 1.3), i + 1.0),       ///< 控制粒子的运动位置和轨迹
                                     smoothRandom(vec2(progress * 0.5), i + 6.25))) *
                         mix(20., 70., rand(i)));                                       ///< 影响粒子的大小，值越大粒子越小
 
    }
    f += 4.;
    f /= 11.;  ///< 变暗
 
    /// @note 颜色随着 progress 而变化
    f = pow3(f * color,                  ///< 着色
             vec3(1., 2. - sin(progress * PI), 1.3)); ///< 1., [2., 1.], 1.3
    f *= sin(progress * PI);
 
 
    /// @note 图像周期性缩放
    p -= .5;    ///< 以屏幕中心为原点
    /// 随机对纹理坐标进行缩放
    p *= 1. + (smoothRandom(vec2(progress * 5.), 6.3) * sin(progress * PI) * .05);
    p += .5;    ///< 平移原点回左下角
 
 
    vec4 blurred_image = vec4(0.);
 
    /// @note 带噪点的转场效果
    float bluramount = sin(progress * PI) * .03;
 
    /// @note repeats 越大，毛玻璃效果越弱
    for (float i = 0.; i < repeats; i++)
    {
        /// 角度转弧度
        float rad = radians((i / repeats) * 360.);
        vec2 q = vec2(cos(rad), sin(rad)) *
                 (rand(vec2(i, p.x + p.y)) + bluramount); ///< 生成噪点
 
        vec2 uv2 = p + (q * bluramount); ///< 随机噪点偏移纹理坐标，毛玻璃效果
        blurred_image += textureSmoothMix(uv2); ///< 叠加随机偏移的纹理（同时随着 progress 变化）
 
    }
    blurred_image /= repeats; ///< 平均，模糊
 
 
    return blurred_image + vec4(f, 0.); ///< 毛玻璃+粒子
 
}
 
//void main()
//{
//    vec2 uv = gl_FragCoord.xy / iResolution.xy;
    // uv.x *= iResolution.x/iResolution.y;
 
//    progress = fract(.5 * iTime); ///< 线性速度
 
//    gl_FragColor = transition(uv);
//}
