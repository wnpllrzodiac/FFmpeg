#include "libavutil/opt.h"
#include "internal.h"
#include "glutil.h"
#include "FreeImage.h"

// sudo apt-get install libfreeimage-dev
// ./ffplay r3.mp4 -vf glmask=tex_count=48:tex_path_fmt='libavfilter/oglfilter/1/a_%d.png' -an

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef __ANDROID__
#include <GL/glew.h>
#endif
#endif

#ifdef GL_TRANSITION_USING_EGL
# include <EGL/egl.h>
# include <EGL/eglext.h>
#else
# include <GLFW/glfw3.h>
#endif

#ifdef GL_TRANSITION_USING_EGL
#ifdef __ANDROID__
static const EGLint configAttribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,// very important!
    EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,//EGL_WINDOW_BIT EGL_PBUFFER_BIT we will create a pixelbuffer surface
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    //EGL_ALPHA_SIZE, 8,// if you need the alpha channel
    EGL_DEPTH_SIZE, 8,// if you need the depth buffer
    EGL_STENCIL_SIZE,8,
    EGL_NONE
};
#else
static const EGLint configAttribs[] = {
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_DEPTH_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
    EGL_NONE};
#endif
#endif

/*
2,3     5

0       1,4
*/
static const float position[12] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    -1.0f, 1.0f,
    1.0f, -1.0f,
    1.0f, 1.0f};

static const GLchar *v_shader_source =
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_source =
    "#version 130\n"
    "precision highp float;\n"
    "\n"
    "varying vec2 texCoord;\n"
    "\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D mask_tex;\n"
    "\n"
    "uniform vec2 direction;\n"
    "uniform int blendMode;\n"
    "uniform float alphaFactor;\n"
    "\n"
    "// normal\n"
    "vec3 blendNormal(vec3 base, vec3 blend) {\n"
    "    return blend;\n"
    "}\n"
    "\n"
    "vec3 blendNormal(vec3 base, vec3 blend, float opacity) {\n"
    "    return (blendNormal(base, blend) * opacity + blend * (1.0 - opacity));\n"
    "}\n"
    "// add\n"
    "float blendAdd(float base, float blend) {\n"
    "    return min(base+blend,1.0);\n"
    "}\n"
    "\n"
    "vec3 blendAdd(vec3 base, vec3 blend) {\n"
    "    return min(base+blend,vec3(1.0));\n"
    "}\n"
    "\n"
    "// average\n"
    "vec3 blendAverage(vec3 base, vec3 blend) {\n"
    "    return (base+blend)/2.0;\n"
    "}\n"
    "\n"
    "// color burn\n"
    "float blendColorBurn(float base, float blend) {\n"
    "    return (blend==0.0)?blend:max((1.0-((1.0-base)/blend)),0.0);\n"
    "}\n"
    "\n"
    "vec3 blendColorBurn(vec3 base, vec3 blend) {\n"
    "    return vec3(blendColorBurn(base.r,blend.r),blendColorBurn(base.g,blend.g),blendColorBurn(base.b,blend.b));\n"
    "}\n"
    "\n"
    "//color dodge\n"
    "float blendColorDodge(float base, float blend) {\n"
    "    return (blend==1.0)?blend:min(base/(1.0-blend),1.0);\n"
    "}\n"
    "\n"
    "vec3 blendColorDodge(vec3 base, vec3 blend) {\n"
    "    return vec3(blendColorDodge(base.r,blend.r),blendColorDodge(base.g,blend.g),blendColorDodge(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// darken\n"
    "float blendDarken(float base, float blend) {\n"
    "    return min(blend,base);\n"
    "}\n"
    "\n"
    "vec3 blendDarken(vec3 base, vec3 blend) {\n"
    "    return vec3(blendDarken(base.r,blend.r),blendDarken(base.g,blend.g),blendDarken(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// difference\n"
    "vec3 blendDifference(vec3 base, vec3 blend) {\n"
    "    return abs(base-blend);\n"
    "}\n"
    "\n"
    "// exclusion\n"
    "vec3 blendExclusion(vec3 base, vec3 blend) {\n"
    "    return base+blend-2.0*base*blend;\n"
    "}\n"
    "\n"
    "// reflect\n"
    "float blendReflect(float base, float blend) {\n"
    "    return (blend==1.0)?blend:min(base*base/(1.0-blend),1.0);\n"
    "}\n"
    "\n"
    "vec3 blendReflect(vec3 base, vec3 blend) {\n"
    "    return vec3(blendReflect(base.r,blend.r),blendReflect(base.g,blend.g),blendReflect(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// glow\n"
    "vec3 blendGlow(vec3 base, vec3 blend) {\n"
    "    return blendReflect(blend,base);\n"
    "}\n"
    " \n"
    "// hard light\n"
    "float blendHardLight(float base, float blend) {\n"
    "    return base<0.5?(2.0*base*blend):(1.0-2.0*(1.0-base)*(1.0-blend));\n"
    "}\n"
    "\n"
    "vec3 blendHardLight(vec3 base, vec3 blend) {\n"
    "    return vec3(blendHardLight(base.r,blend.r),blendHardLight(base.g,blend.g),blendHardLight(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// hard mix\n"
    "float blendHardMix(float base, float blend) {\n"
    "    if(blend<0.5) {\n"
    "        float vividLight = blendColorBurn(base,(2.0*blend));\n"
    "        return (vividLight < 0.5 ) ? 0.0:1.0;\n"
    "    } else {\n"
    "        float vividLight = blendColorDodge(base,(2.0*(blend-0.5)));\n"
    "        return (vividLight < 0.5 ) ? 0.0:1.0;\n"
    "    }\n"
    "}\n"
    "\n"
    "vec3 blendHardMix(vec3 base, vec3 blend) {\n"
    "    return vec3(blendHardMix(base.r,blend.r),blendHardMix(base.g,blend.g),blendHardMix(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// lighten\n"
    "float blendLighten(float base, float blend) {\n"
    "    return max(blend,base);\n"
    "}\n"
    "\n"
    "vec3 blendLighten(vec3 base, vec3 blend) {\n"
    "    return vec3(blendLighten(base.r,blend.r),blendLighten(base.g,blend.g),blendLighten(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// linear burn \n"
    "float blendLinearBurn(float base, float blend) {\n"
    "    return max(base+blend-1.0,0.0);\n"
    "}\n"
    "\n"
    "vec3 blendLinearBurn(vec3 base, vec3 blend) {\n"
    "    return max(base+blend-vec3(1.0),vec3(0.0));\n"
    "}\n"
    "\n"
    "// linear dodge\n"
    "float blendLinearDodge(float base, float blend) {\n"
    "    return min(base+blend,1.0);\n"
    "}\n"
    "\n"
    "vec3 blendLinearDodge(vec3 base, vec3 blend) {\n"
    "    return min(base+blend,vec3(1.0));\n"
    "}\n"
    "\n"
    "// linear light\n"
    "float blendLinearLight(float base, float blend) {\n"
    "    return blend<0.5?blendLinearBurn(base,(2.0*blend)):blendLinearDodge(base,(2.0*(blend-0.5)));\n"
    "}\n"
    "\n"
    "vec3 blendLinearLight(vec3 base, vec3 blend) {\n"
    "    return vec3(blendLinearLight(base.r,blend.r),blendLinearLight(base.g,blend.g),blendLinearLight(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// multiply\n"
    "vec3 blendMultiply(vec3 base, vec3 blend) {\n"
    "    return base*blend;\n"
    "}\n"
    "\n"
    "// negation\n"
    "vec3 blendNegation(vec3 base, vec3 blend) {\n"
    "    return vec3(1.0)-abs(vec3(1.0)-base-blend);\n"
    "}\n"
    "\n"
    "// overlay\n"
    "float blendOverlay(float base, float blend) {\n"
    "    return base<0.5?(2.0*base*blend):(1.0-2.0*(1.0-base)*(1.0-blend));\n"
    "}\n"
    "\n"
    "vec3 blendOverlay(vec3 base, vec3 blend) {\n"
    "    return vec3(blendOverlay(base.r,blend.r),blendOverlay(base.g,blend.g),blendOverlay(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// phoenix\n"
    "vec3 blendPhoenix(vec3 base, vec3 blend) {\n"
    "    return min(base,blend)-max(base,blend)+vec3(1.0);\n"
    "}\n"
    "\n"
    "// pin light\n"
    "float blendPinLight(float base, float blend) {\n"
    "    return (blend<0.5)?blendDarken(base,(2.0*blend)):blendLighten(base,(2.0*(blend-0.5)));\n"
    "}\n"
    "\n"
    "vec3 blendPinLight(vec3 base, vec3 blend) {\n"
    "    return vec3(blendPinLight(base.r,blend.r),blendPinLight(base.g,blend.g),blendPinLight(base.b,blend.b));\n"
    "}\n"
    "\n"
    "\n"
    "// screen\n"
    "float blendScreen(float base, float blend) {\n"
    "    return 1.0-((1.0-base)*(1.0-blend));\n"
    "}\n"
    "\n"
    "vec3 blendScreen(vec3 base, vec3 blend) {\n"
    "    return vec3(blendScreen(base.r,blend.r),blendScreen(base.g,blend.g),blendScreen(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// soft light\n"
    "float blendSoftLight(float base, float blend) {\n"
    "    return (blend<0.5)?(2.0*base*blend+base*base*(1.0-2.0*blend)):(sqrt(base)*(2.0*blend-1.0)+2.0*base*(1.0-blend));\n"
    "}\n"
    "\n"
    "vec3 blendSoftLight(vec3 base, vec3 blend) {\n"
    "    return vec3(blendSoftLight(base.r,blend.r),blendSoftLight(base.g,blend.g),blendSoftLight(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// substract\n"
    "float blendSubstract(float base, float blend) {\n"
    "    return max(base+blend-1.0,0.0);\n"
    "}\n"
    "\n"
    "vec3 blendSubstract(vec3 base, vec3 blend) {\n"
    "    return max(base+blend-vec3(1.0),vec3(0.0));\n"
    "}\n"
    "\n"
    "// vivid light\n"
    "float blendVividLight(float base, float blend) {\n"
    "    return (blend<0.5)?blendColorBurn(base,(2.0*blend)):blendColorDodge(base,(2.0*(blend-0.5)));\n"
    "}\n"
    "\n"
    "vec3 blendVividLight(vec3 base, vec3 blend) {\n"
    "    return vec3(blendVividLight(base.r,blend.r),blendVividLight(base.g,blend.g),blendVividLight(base.b,blend.b));\n"
    "}\n"
    "\n"
    "// snow color\n"
    "vec3 RGBToHSL(vec3 color){\n"
    "    vec3 hsl;\n"
    "    float fmin = min(min(color.r, color.g), color.b);\n"
    "    float fmax = max(max(color.r, color.g), color.b);\n"
    "    float delta = fmax - fmin;\n"
    "    \n"
    "    hsl.z = (fmax + fmin) / 2.0;\n"
    "    \n"
    "    if (delta == 0.0)\n"
    "    {\n"
    "        hsl.x = 0.0;\n"
    "        hsl.y = 0.0;\n"
    "    }\n"
    "    else\n"
    "    {\n"
    "        if (hsl.z < 0.5)\n"
    "            hsl.y = delta / (fmax + fmin);\n"
    "        else\n"
    "            hsl.y = delta / (2.0 - fmax - fmin);\n"
    "        \n"
    "        float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;\n"
    "        float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;\n"
    "        float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;\n"
    "        \n"
    "        if (color.r == fmax )\n"
    "            hsl.x = deltaB - deltaG;\n"
    "        else if (color.g == fmax)\n"
    "            hsl.x = (1.0 / 3.0) + deltaR - deltaB;\n"
    "        else if (color.b == fmax)\n"
    "            hsl.x = (2.0 / 3.0) + deltaG - deltaR;\n"
    "        \n"
    "        if (hsl.x < 0.0)\n"
    "            hsl.x += 1.0;\n"
    "        else if (hsl.x > 1.0)\n"
    "            hsl.x -= 1.0;\n"
    "    }\n"
    "    \n"
    "    return hsl;\n"
    "}\n"
    "\n"
    "float HueToRGB(float f1, float f2, float hue){\n"
    "    if (hue < 0.0)\n"
    "        hue += 1.0;\n"
    "    else if (hue > 1.0)\n"
    "        hue -= 1.0;\n"
    "    float res;\n"
    "    if ((6.0 * hue) < 1.0)\n"
    "        res = f1 + (f2 - f1) * 6.0 * hue;\n"
    "    else if ((2.0 * hue) < 1.0)\n"
    "        res = f2;\n"
    "    else if ((3.0 * hue) < 2.0)\n"
    "        res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;\n"
    "    else\n"
    "        res = f1;\n"
    "    return res;\n"
    "}\n"
    "\n"
    "vec3 HSLToRGB(vec3 hsl){\n"
    "    vec3 rgb;\n"
    "    \n"
    "    if (hsl.y == 0.0)\n"
    "        rgb = vec3(hsl.z);\n"
    "    else\n"
    "    {\n"
    "        float f2;\n"
    "        \n"
    "        if (hsl.z < 0.5)\n"
    "            f2 = hsl.z * (1.0 + hsl.y);\n"
    "        else\n"
    "            f2 = (hsl.z + hsl.y) - (hsl.y * hsl.z);\n"
    "        \n"
    "        float f1 = 2.0 * hsl.z - f2;\n"
    "        \n"
    "        rgb.r = HueToRGB(f1, f2, hsl.x + (1.0/3.0));\n"
    "        rgb.g = HueToRGB(f1, f2, hsl.x);\n"
    "        rgb.b= HueToRGB(f1, f2, hsl.x - (1.0/3.0));\n"
    "    }\n"
    "    \n"
    "    return rgb;\n"
    "}\n"
    "\n"
    "vec3 blendSnowColor(vec3 blend, vec3 bgColor) {\n"
    "    vec3 blendHSL = RGBToHSL(blend);\n"
    "    vec3 hsl = RGBToHSL(bgColor);\n"
    "    return HSLToRGB(vec3(blendHSL.r, blendHSL.g, hsl.b));\n"
    "}\n"
    "\n"
    "// snow hue\n"
    "vec3 blendSnowHue(vec3 blend, vec3 bgColor) {\n"
    "    vec3 baseHSL = RGBToHSL(bgColor.rgb);\n"
    "    return HSLToRGB(vec3(RGBToHSL(blend.rgb).r, baseHSL.g, baseHSL.b));\n"
    "}\n"
    "\n"
    "vec3 blendFunc(vec3 base, vec3 blend, float opacity,int blendMode) {\n"
    "    if (blendMode == 0)\n"
    "        return (blendNormal(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 1)\n"
    "    	return (blendAdd(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 2)\n"
    "    	return (blendAverage(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 3)\n"
    "    	return (blendColorBurn(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 4)\n"
    "		return (blendColorDodge(base, blend) * opacity + base * (1.0 - opacity));   \n"
    "	else if (blendMode == 5)\n"
    "		return (blendDarken(base, blend) * opacity + base * (1.0 - opacity)); \n"
    "	else if (blendMode == 6)\n"
    "		return (blendDifference(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 7)\n"
    "		return (blendExclusion(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 8)\n"
    "		return (blendGlow(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 9)\n"
    "		return (blendHardLight(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 10)\n"
    "		return (blendHardMix(base, blend) * opacity + base * (1.0 - opacity));	\n"
    "	else if (blendMode == 11)\n"
    "		return (blendLighten(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 12)\n"
    "		return (blendLinearBurn(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 13)\n"
    "		return (blendLinearDodge(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 14)\n"
    "		return (blendLinearLight(base, blend) * opacity + base * (1.0 - opacity));\n"
    "	else if (blendMode == 15)\n"
    "		return (blendMultiply(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 16)\n"
    "        return (blendNegation(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 17)\n"
    "        return (blendOverlay(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 18)\n"
    "        return (blendPhoenix(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 19)\n"
    "        return (blendPinLight(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 20)\n"
    "        return (blendReflect(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 21)\n"
    "        return (blendScreen(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 22)\n"
    "        return (blendSoftLight(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 23)\n"
    "        return (blendSubstract(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 24)\n"
    "        return (blendVividLight(base, blend) * opacity + base * (1.0 - opacity));\n"
    "    else if (blendMode == 25)\n"
    "        return blendSnowColor(blend, blend);\n"
    "    else if (blendMode == 26)\n"
    "        return blendSnowHue(blend, blend);\n"
    "    else \n"
    "        return base;\n"
    "}\n"
    "\n"
    "// int floatToInt(float f) {\n"
    "//     return fract(f) >= 0.5 ? int(ceil(f)) : int(floor(f));\n"
    "// }\n"
    "\n"
    "void main(void) \n"
    "{\n"
    "    vec2 uv = vec2(texCoord.x, 1.0-texCoord.y);\n"
    "    vec4 fgColor = texture2D(mask_tex, uv);\n"
    "    fgColor = fgColor * alphaFactor;\n"
    "        \n"
    "    vec4 bgColor = texture2D(tex, uv + direction);\n"
    "    vec3 color = blendFunc(bgColor.rgb, clamp(fgColor.rgb * (1.0 / fgColor.a), 0.0, 1.0), 1.0, blendMode);\n"
    "    gl_FragColor = vec4(bgColor.rgb * (1.0 - fgColor.a) + color.rgb * fgColor.a, bgColor.a);\n"
    "}\n"
    ;

#define PIXEL_FORMAT GL_RGB

    typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLuint mask_tex;
    GLuint pos_buf;

    GLint time;
    int no_window;
    int mode;
    double alpha;

    uint8_t *mask_data;
    int mask_pic_num;
    char *mask_pic_fmt;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlMaskContext;

#define OFFSET(x) offsetof(GlMaskContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption glmask_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"alpha", "blend alpha value [0.0, 1.0]", OFFSET(alpha), AV_OPT_TYPE_DOUBLE, {.dbl = 0.9}, 0, 1.0, FLAGS},
    {"mode", "blend mode [0, 26]", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 26, FLAGS},
    {"tex_count", "total texture file count", OFFSET(mask_pic_num), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 200, FLAGS},
    { "tex_path_fmt", "texture file format, e.g.: image/%d.png", OFFSET(mask_pic_fmt), AV_OPT_TYPE_STRING, {.str = "0"}, 0, 0, FLAGS },
    {NULL}};

AVFILTER_DEFINE_CLASS(glmask);

static GLuint build_shader(AVFilterContext *ctx, const GLchar *shader_source, GLenum type)
{
    GLuint shader = glCreateShader(type);
    if (!shader || !glIsShader(shader))
    {
        av_log(NULL, AV_LOG_ERROR, "failed to create shader: %d", shader);
        return 0;
    }
    glShaderSource(shader, 1, &shader_source, 0);
    glCompileShader(shader);

    GLint compileResult = GL_TRUE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult);
    if (compileResult == GL_FALSE)
    {
        char szLog[1024] = {0};
        GLsizei logLen = 0;
        glGetShaderInfoLog(shader, 1024, &logLen, szLog);
        av_log(NULL, AV_LOG_ERROR, "Compile Shader fail error log: %s \nshader code:\n%s\n",
               szLog, shader_source);
        glDeleteShader(shader);
        shader = 0;
    }

    return compileResult == GL_TRUE ? shader : 0;
}

static void vbo_setup(GlMaskContext *gs)
{
    glGenBuffers(1, &gs->pos_buf);
    glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

    GLint loc = glGetAttribLocation(gs->program, "position");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static int tex_setup(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlMaskContext *gs = ctx->priv;

    glGenTextures(1, &gs->frame_tex);
    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "tex"), 0);

    // mask
    glGenTextures(1, &gs->mask_tex);
    glActiveTexture(GL_TEXTURE0 + 1);

    glBindTexture(GL_TEXTURE_2D, gs->mask_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 500/*w*/, 500/*h*/, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "mask_tex"), 1);

    if (gs->mask_pic_fmt == NULL || gs->mask_pic_num == 0 || gs->mask_pic_num > 256) {
        av_log(NULL, AV_LOG_ERROR, "invalid mask pic settings\n");
        return -1;
    }

    for (int i=0 ; i < gs->mask_pic_num ; i++) {
        char filename[256] = {0};
        sprintf(filename, gs->mask_pic_fmt, i);
        FIBITMAP *img = FreeImage_Load(FIF_PNG, filename, 0);
        if (!img) {
            av_log(NULL, AV_LOG_ERROR, "failed to open image file: %s\n", filename);
            return -1;
        }

        int w = FreeImage_GetWidth(img);
        int h = FreeImage_GetHeight(img);
        int bpp = FreeImage_GetBPP(img);
        //av_log(NULL, AV_LOG_DEBUG, "img res: %d x %d, bpp %d\n", w, h, bpp);
        if (w <= 0 || h <= 0 || bpp <= 0) {
            av_log(NULL, AV_LOG_ERROR, "failed to get image file info: %s\n", filename);
            return -1;
        }

        if (!gs->mask_data)
            gs->mask_data = av_mallocz(w * h * bpp / 8 * gs->mask_pic_num);

        BYTE *data = FreeImage_GetBits(img);
        if (!data) {
            av_log(NULL, AV_LOG_ERROR, "failed to get image data: %s\n", filename);
            return -1;
        }

        memcpy(gs->mask_data + w * h * bpp / 8 * i, data, w * h * bpp / 8);
        FreeImage_Unload(img);
    }

    return 0;
}

static int build_program(AVFilterContext *ctx)
{
    GLuint v_shader, f_shader;
    GlMaskContext *gs = ctx->priv;

    if (!((v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER)) &&
          (f_shader = build_shader(ctx, f_shader_source, GL_FRAGMENT_SHADER))))
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build shader: vsh: %d, fsh: %d\n", v_shader, f_shader);
        return -1;
    }

    gs->program = glCreateProgram();
    glAttachShader(gs->program, v_shader);
    glAttachShader(gs->program, f_shader);
    glLinkProgram(gs->program);

    GLint status;
    glGetProgramiv(gs->program, GL_LINK_STATUS, &status);
    return status == GL_TRUE ? 0 : -1;
}

static av_cold int init(AVFilterContext *ctx)
{
    GlMaskContext *gs = ctx->priv;

#ifndef GL_TRANSITION_USING_EGL
    if (gs->no_window)
    {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
#endif

    return 0;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    AVFilterContext *ctx = fromLink->dst;
    GlMaskContext *gs = ctx->priv;

    gs->time = glGetUniformLocation(gs->program, "time");
    glUniform1f(gs->time, 0.0f);

    glUniform1f(glGetUniformLocation(gs->program, "alphaFactor"), (float)gs->alpha);
    glUniform1f(glGetUniformLocation(gs->program, "mode"), gs->mode);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlMaskContext *gs = ctx->priv;

#ifdef GL_TRANSITION_USING_EGL
    //init EGL
    // 1. Initialize EGL
    // c->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);

#ifdef __ANDROID__
    gs->eglDpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (gs->eglDpy == EGL_NO_DISPLAY) {
        av_log(NULL, AV_LOG_ERROR, "eglGetDisplay error\n");
        return -1;
    }
#else
#define MAX_DEVICES 4
    EGLDeviceEXT eglDevs[MAX_DEVICES];
    EGLint numDevices;

    PFNEGLQUERYDEVICESEXTPROC eglQueryDevicesEXT =(PFNEGLQUERYDEVICESEXTPROC)
    eglGetProcAddress("eglQueryDevicesEXT");

    eglQueryDevicesEXT(MAX_DEVICES, eglDevs, &numDevices);

    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =  (PFNEGLGETPLATFORMDISPLAYEXTPROC)
    eglGetProcAddress("eglGetPlatformDisplayEXT");

    gs->eglDpy = eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, eglDevs[0], 0);
#endif

    EGLint major, minor;
    eglInitialize(gs->eglDpy, &major, &minor);
    av_log(ctx, AV_LOG_DEBUG, "%d.%d", major, minor);
    
    // 2. Select an appropriate configuration
    EGLint numConfigs;
    EGLint pbufferAttribs[] = {
        EGL_WIDTH,
        inlink->w,
        EGL_HEIGHT,
        inlink->h,
        EGL_NONE,
    };
    //3.1 根据所需的参数获取符合该参数的config_size，主要是解决有些手机eglChooseConfig失败的兼容性问题
    if (!eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, 1, &numConfigs)) {
        av_log(NULL, AV_LOG_ERROR, "eglChooseConfig error\n");
        return -1;
    }
#ifdef __ANDROID__
    //3.2 根据获取到的config_size得到eglConfig
    av_log(NULL, AV_LOG_ERROR, "numConfigs: %d\n", numConfigs);
    if (!eglChooseConfig(gs->eglDpy, configAttribs, &gs->eglCfg, numConfigs, &numConfigs)) {
        av_log(NULL, AV_LOG_ERROR, "eglChooseConfig error\n");
        return -1;
    }
#endif
    // 3. Create a surface
    gs->eglSurf = eglCreatePbufferSurface(gs->eglDpy, gs->eglCfg, pbufferAttribs);
    if(gs->eglSurf == EGL_NO_SURFACE) {
        switch(eglGetError())
        {
            case EGL_BAD_ALLOC:
                // Not enough resources available. Handle and recover
                av_log(NULL, AV_LOG_ERROR, "Not enough resources available\n");
                break;
            case EGL_BAD_CONFIG:
                // Verify that provided EGLConfig is valid
                av_log(NULL, AV_LOG_ERROR, "provided EGLConfig is invalid\n");
                break;
            case EGL_BAD_PARAMETER:
                // Verify that the EGL_WIDTH and EGL_HEIGHT are
                // non-negative values
                av_log(NULL, AV_LOG_ERROR, "provided EGL_WIDTH and EGL_HEIGHT is invalid\n");
                break;
            case EGL_BAD_MATCH:
                // Check window and EGLConfig attributes to determine
                // compatibility and pbuffer-texture parameters
                av_log(NULL, AV_LOG_ERROR, "Check window and EGLConfig attributes\n");
                break;
        }

        return -1;
    }
    // 4. Bind the API
    eglBindAPI(EGL_OPENGL_API);
    // 5. Create a context and make it current
#ifdef __ANDROID__
    const EGLint attrib_ctx_list[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };
    gs->eglCtx = eglCreateContext(gs->eglDpy, gs->eglCfg, NULL, attrib_ctx_list);
#else
    gs->eglCtx = eglCreateContext(gs->eglDpy, gs->eglCfg, EGL_NO_CONTEXT, NULL);
#endif
    if (gs->eglCtx == EGL_NO_CONTEXT) {
        av_log(NULL, AV_LOG_ERROR, "eglCreateContext  error\n");
        return -1;
    }
    if (!eglMakeCurrent(gs->eglDpy, gs->eglSurf, gs->eglSurf, gs->eglCtx)) {
        av_log(NULL, AV_LOG_ERROR, "eglMakeCurrent  error\n");
        return -1;
    }
#else
    //glfw
    glfwWindowHint(GLFW_VISIBLE, 0);
    gs->window = glfwCreateWindow(inlink->w, inlink->h, "", NULL, NULL);

    glfwMakeContextCurrent(gs->window);
#endif

#if !defined(__APPLE__) && !defined(__ANDROID__)
    glewExperimental = GL_TRUE;
    glewInit();
#endif

    glViewport(0, 0, inlink->w, inlink->h);

    int ret;
    if ((ret = build_program(ctx)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build ogl program: %d\n", ret);
        return ret;
    }

    glUseProgram(gs->program);
    vbo_setup(gs);
    setup_uniforms(inlink);
    if (tex_setup(inlink) < 0) {
        av_log(NULL, AV_LOG_ERROR, "failed to setup texture\n");
        return -1;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlMaskContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    const float time = in->pts * av_q2d(inlink->time_base);
    glUniform1f(gs->time, time);

    int direction[5][2] = {{-1,-1},{-1,1},{1,1},{1,-1},{0,0}};
    float intensity = 0.0035*2.0;
    float fps = 25;
    int id = (int)(time * fps);    
    id = id % 5;
    /*id = id % 10 + 1
    if (id > 5) {
        id -= 10;
        if (id == 0)
            id = 5;
    }*/
    
    glUniform2f(glGetUniformLocation(gs->program, "direction"), 
        direction[id][0]*intensity,
        direction[id][1]*intensity);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, outlink->w, outlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, gs->mask_tex);
    int idx = (int)(time * 1000.0 / 40.0) % 48;
    int offset = 500 * 500 * 4 * idx;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 500, 500, 0, GL_RGBA, GL_UNSIGNED_BYTE, gs->mask_data + offset);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlMaskContext *gs = ctx->priv;

#ifdef GL_TRANSITION_USING_EGL
    if (gs->eglDpy) {
        glDeleteTextures(1, &gs->frame_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        eglTerminate(gs->eglDpy);
    }
#else
    if (gs->window) {
        glDeleteTextures(1, &gs->frame_tex);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        glfwDestroyWindow(gs->window);
    }
#endif

    if (gs->mask_data) {
        av_free(gs->mask_data);
        gs->mask_data = NULL;
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    return ff_set_common_formats(ctx, ff_make_format_list(formats));
}

static const AVFilterPad glmask_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad glmask_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_glmask = {
    .name = "glmask",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter mask"),
    .priv_size = sizeof(GlMaskContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = glmask_inputs,
    .outputs = glmask_outputs,
    .priv_class = &glmask_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
