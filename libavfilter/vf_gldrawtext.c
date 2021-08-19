#include "libavutil/bprint.h"
#include "libavutil/eval.h"
#include "libavutil/file.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "libavutil/tree.h"
#include "libavutil/parseutils.h"
#include "drawutils.h"
#include "internal.h"
#include "glutil.h"
#include "lavfutils.h"
#include "libavutil/pixdesc.h"

// ./ffplay /mnt/d/Archive/Media/TimeCode.mov -vf scale=640x480,gldrawtext=text='太太太太重要了!!!':
//   fontsize=36:fontcolor=white:fontfile=local_华康金刚黑.ttf:x=100:y=200:borderw=6:bordercolor=red -an

// ffmpeg -y -f lavfi -i color=c=green:size=320x240 -vf gldrawtext=text='太太太太重要了!!!':
//    fontsize=48:y=4:fontcolor=white:fontfile=msyh.ttc:borderw=3:bordercolor=red,chromakey=color=green:similarity=0.15 -t 5 -f apng out.png
// ffmpeg -i /mnt/d/Archive/Media/timecode.3gp -i out.png -filter_complex [0:v][1:v]overlay[outv] 
//    -map [outv] -map 0:a -c:v libx264 -b:v 512k -c:a copy -t 10 out.mp4

// support alpha png
// ./ffmpeg -y -i ~/michael/media/d_9.mp4 -filter_complex 
// "color=c=black@0:s=760x144,format=rgba,gldrawtext=text=AaopglH李二龙:fontfile=mtr.ttf:
// fontsize=128:fontcolor=red@1:alphachannel=1"
// -pix_fmt rgba -frames:v 1 ~/michael/tools/nginx/html/tmp/1.png

// set bg
// ./ffmpeg -y -i ~/michael/media/d_9.mp4 -filter_complex 
// "color=c=black@0:s=760x144,format=rgba,gldrawtext=text=AaopglH李二龙:fontfile=mtr.ttf:
// fontsize=128:fontcolor=red@1:alphachannel=1:texture=./libavfilter/oglfilter/resource/lieheng.png:borderw=10:bordercolor=white"
// -pix_fmt rgba -frames:v 1 ~/michael/tools/nginx/html/tmp/1.png

#if CONFIG_LIBFONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

#if CONFIG_LIBFRIBIDI
#include <fribidi.h>
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#ifndef __ANDROID__
#include <GL/glew.h>
#include <GL/glx.h>
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
    -1.0f,  1.0f, 
    -1.0f,  1.0f, 
     1.0f, -1.0f,
     1.0f,  1.0f
};

static const GLchar *v_shader_source =
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_source =
    "uniform sampler2D tex;\n"
    "uniform sampler2D text_tex;\n"
    "varying vec2 texCoord;\n"
    "void main() {\n"
    "  vec2 uv = vec2(texCoord.x, 1.0 - texCoord.y);\n"
    "  vec4 text_c = texture2D(tex, uv);\n"
    "  vec4 texture_c = texture2D(text_tex, uv);\n"
    "  if (text_c.r > 0.99 && text_c.g < 0.01 && text_c.b < 0.01)\n"
    //"  if (uv.x < 0.5)\n"
    "    gl_FragColor = texture_c;\n"
    "  else\n"
    "    gl_FragColor = text_c;\n"
    //"  gl_FragColor = text_c;\n"
    "}\n";

static const GLchar *f_shader_source_wipe_right = // 向右擦除
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "uniform int u_Time;\n"
    "uniform vec2 eraseUV;\n"
    "\n"
    "void main() {\n"
    "  vec2 uv = vec2(texCoord.x, 1.0 - texCoord.y);\n"
    "  if (uv.x > eraseUV.x)\n"
    "      gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "  else\n"
    "      gl_FragColor = texture2D(tex, uv);\n"
    "}\n";

static const GLchar *f_shader_source_bad_typewriter = // 故障打字机
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "uniform int u_Time;\n"
    "uniform float deg;\n"
    "\n"
    "void main() {\n"
    "  vec2 uv = vec2(texCoord.x, 1.0 - texCoord.y);\n"
    "  vec4 curColor = texture2D(tex, uv);\n"
    "  float progress = deg;\n"
    "  float cur_progress = progress*0.5;\n"
    "  vec4 resultColor = curColor;\n"
    "  if(uv.x<0.5-cur_progress) {\n"
    "      vec4 edgeColor = texture2D(tex, vec2(0.5-cur_progress,uv.y));\n"
    "      resultColor = edgeColor;\n"
    "  }\n"
    "  else if(uv.x>0.5+cur_progress) {\n"
    "      vec4 edgeColor = texture2D(tex, vec2(0.5+cur_progress,uv.y));\n"
    "      resultColor = edgeColor;\n"
    "  }\n"
    "  gl_FragColor = resultColor;\n"
    "}\n";

static const GLchar *f_shader_source_blur = // 模糊
    "#version 130\n"
    "precision highp float;\n"
    "\n"
    "#define ANIMSEQ 0\n"
    "\n"
    "uniform sampler2D tex;\n"
    "varying vec2 texCoord;\n"
    "uniform vec2 u_Resolution;\n"
    "\n"
    "uniform vec3 param;\n"
    "uniform vec2 blurDirection;\n"
    "uniform float blurStep;\n"
    "uniform float alpha;\n"
    "\n"
    "#define BLUR_MOTION 0x1\n"
    "#define BLUR_SCALE  0x2\n"
    "#define BLUR_TYPE BLUR_MOTION\n"
    "\n"
    "#if BLUR_TYPE == BLUR_SCALE\n"
    "#define num 25\n"
    "#else\n"
    "#define num 15\n"
    "#endif\n"
    "\n"
    "float random(in vec3 scale, in float seed) {\n"
    "    /* use the fragment position for randomness */\n"
    "    return fract(sin(dot(gl_FragCoord.xyz + seed, scale)) * 43758.5453 + seed);\n"
    "}\n"
    "\n"
    "vec4 directionBlur(sampler2D tex, vec2 resolution, vec2 uv, vec2 directionOfBlur, float intensity)\n"
    "{\n"
    "    vec2 pixelStep = 1.0/resolution * intensity;\n"
    "    float dircLength = max(length(directionOfBlur), .000001);\n"
    "	pixelStep.x = directionOfBlur.x * 1.0 / dircLength * pixelStep.x;\n"
    "	pixelStep.y = directionOfBlur.y * 1.0 / dircLength * pixelStep.y;\n"
    "\n"
    "	vec4 color = vec4(0);\n"
    "	for(int i = -num; i <= num; i++)\n"
    "	{\n"
    "        vec2 blurCoord = uv + pixelStep * float(i);\n"
    "        // vec2 uvT = vec2(1.0 - abs(abs(blurCoord.x) - 1.0), 1.0 - abs(abs(blurCoord.y) - 1.0));\n"
    "#if ANIMSEQ == 1\n"
    "        blurCoord.x = clamp(blurCoord.x, 0., 1.);\n"
    "        blurCoord.y = clamp(blurCoord.y, 0., 1.);\n"
    "        blurCoord = blurCoord * _MainTex_ST.xy + _MainTex_ST.zw;\n"
    "#endif\n"
    "        color += texture2D(tex, blurCoord);\n"
    "	}\n"
    "	color /= float(2 * num + 1);	\n"
    "	return color;\n"
    "}\n"
    "\n"
    "vec4 scaleBlur(vec2 uv) {\n"
    "    vec4 color = vec4(0.0);\n"
    "    float total = 0.0;\n"
    "	vec2 toCenter = vec2(0.5, 0.5) - uv;\n"
    "    float dissolve = 0.5;\n"
    "    /* randomize the lookup values to hide the fixed number of samples */\n"
    "    float offset3 = random(vec3(12.9898, 78.233, 151.7182), 0.0);\n"
    "\n"
    "    for (int t = 0; t <= num; t++) {\n"
    "        float percent = (float(t) + offset3 - .5) / float(num);\n"
    "        float weight = 4.0 * (percent - percent * percent);\n"
    "\n"
    "		vec2 curUV = uv + toCenter * percent * blurStep;\n"
    "        // vec2 uvT = vec2(1.0 - abs(abs(curUV.x) - 1.0), 1.0 - abs(abs(curUV.y) - 1.0));\n"
    "\n"
    "#if ANIMSEQ == 1\n"
    "        curUV.x = clamp(curUV.x, 0., 1.);\n"
    "        curUV.y = clamp(curUV.y, 0., 1.);\n"
    "        curUV = vec4(curUV, 0.0, 1.0);\n"
    "#endif\n"
    "        color += texture2D(_MainTex, curUV) * weight;\n"
    "        // color += crossFade(uvT + toCenter * percent * blurStep, dissolve) * weight;\n"
    "        total += weight;\n"
    "    }\n"
    "    return color / total;\n"
    "}\n"
    "\n"
    "void main()\n"
    "{\n"
    "    vec2 uv0 = vec2(texCoord.x, 1.0 - texCoord.y);\n"
    "    vec4 color = vec4(0);\n"
    "#if BLUR_TYPE == BLUR_MOTION\n"
    "    color = directionBlur(tex, u_Resolution, uv0, blurDirection, blurStep);\n"
    "#elif BLUR_TYPE == BLUR_SCALE\n"
    "    color = scaleBlur(uv0);\n"
    "#else\n"
    "    vec2 uv = uv0;\n"
    "#if ANIMSEQ == 1\n"
    "    uv.x = clamp(uv.x, 0., 1.);\n"
    "    uv.y = clamp(uv.y, 0., 1.);\n"
    "    uv = vec4(uv, 0.0, 1.0);\n"
    "#endif\n"
    "    color = texture2D(tex, uv);\n"
    "#endif\n"
    "    color *= alpha;\n"
    "    gl_FragColor = color;\n"
    "}\n"
    ;

#undef __FTERRORS_H__
#define FT_ERROR_START_LIST {
#define FT_ERRORDEF(e, v, s) { (e), (s) },
#define FT_ERROR_END_LIST { 0, NULL } };

static const char *const fun2_names[] = {
    "rand"
};

static double drand(void *opaque, double min, double max)
{
    return min + (max-min) / UINT_MAX * av_lfg_get(opaque);
}

typedef double (*eval_func2)(void *, double a, double b);

static const eval_func2 fun2[] = {
    drand,
    NULL
};

static const char *const var_names[] = {
    "line_h", "lh",           ///< line height, same as max_glyph_h
    "main_h", "h", "H",       ///< height of the input video
    "main_w", "w", "W",       ///< width  of the input video
    "max_glyph_a", "ascent",  ///< max glyph ascent
    "max_glyph_d", "descent", ///< min glyph descent
    "max_glyph_h",            ///< max glyph height
    "max_glyph_w",            ///< max glyph width
    "text_h", "th",           ///< height of the rendered text
    "text_w", "tw",           ///< width  of the rendered text
    "x",
    "y",
    NULL
};

enum var_name {
    VAR_LINE_H, VAR_LH,
    VAR_MAIN_H, VAR_h, VAR_H,
    VAR_MAIN_W, VAR_w, VAR_W,
    VAR_MAX_GLYPH_A, VAR_ASCENT,
    VAR_MAX_GLYPH_D, VAR_DESCENT,
    VAR_MAX_GLYPH_H,
    VAR_MAX_GLYPH_W,
    VAR_TEXT_H, VAR_TH,
    VAR_TEXT_W, VAR_TW,
    VAR_X,
    VAR_Y,
    VAR_VARS_NB
};

static const struct ft_error {
    int err;
    const char *err_msg;
} ft_errors[] =
#include FT_ERRORS_H

#define FT_ERRMSG(e) ft_errors[e].err_msg

typedef struct Glyph {
    FT_Glyph glyph;
    FT_Glyph border_glyph;
    uint32_t code;
    unsigned int fontsize;
    FT_Bitmap bitmap; ///< array holding bitmaps of font
    FT_Bitmap border_bitmap; ///< array holding bitmaps of font border
    FT_BBox bbox;
    int advance;
    int bitmap_left;
    int bitmap_top;
} Glyph;

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    GLuint program;
    GLuint frame_tex;
    GLuint extra_tex;
    GLuint pos_buf;

    GLint time;

#if CONFIG_LIBFONTCONFIG
    uint8_t *font;                  ///< font to be used
#endif
    uint8_t *fontfile;              ///< font to be used
    uint8_t *text;                  ///< text to be drawn
    uint8_t *fontcolor_expr;        ///< fontcolor expression to evaluate
    AVBPrint expanded_fontcolor;    ///< used to contain the expanded fontcolor spec
    FT_Vector *positions;           ///< positions for each element in the text
    size_t nb_positions;            ///< number of elements of positions array
    int ft_load_flags;              ///< flags used for loading fonts, see FT_LOAD_*
    char *textfile;                 ///< file with text to be drawn
    int x;                          ///< x position to start drawing text
    int y;                          ///< y position to start drawing text
    int max_glyph_w;                ///< max glyph width
    int max_glyph_h;                ///< max glyph height
    int shadowx, shadowy;
    int borderw;                    ///< border width
    double var_values[VAR_VARS_NB];
    int alpha;
    AVLFG  prng;                    ///< random

    FFDrawContext dc;
    FFDrawColor fontcolor;          ///< foreground color
    FFDrawColor shadowcolor;        ///< shadow color
    FFDrawColor bordercolor;        ///< border color
    FFDrawColor boxcolor;           ///< background color

    FT_Library library;             ///< freetype font library handle
    FT_Face face;                   ///< freetype font face handle
    FT_Stroker stroker;             ///< freetype stroker handle
    struct AVTreeNode *glyphs;      ///< rendered glyphs, stored using the UTF-32 char code

    char *fontsize_expr;            ///< expression for fontsize
    AVExpr *fontsize_pexpr;         ///< parsed expressions for fontsize
    unsigned int fontsize;          ///< font size to use
    unsigned int default_fontsize;  ///< default font size to use

    int line_spacing;               ///< lines spacing in pixels

    int use_kerning;                ///< font kerning is used - true/false
    int tabsize;                    ///< tab size

    int reload;                     ///< reload text file for each frame
    int start_number;               ///< starting frame number for n/frame_num var

    int no_window;
    int type;

    int alphachannel;
    // decided by alpha
    int pix_fmt;
    int channel_num;

    char *texture_filepath;
    uint8_t *tex_data;

#ifdef GL_TRANSITION_USING_EGL
    EGLDisplay      eglDpy;
    EGLConfig       eglCfg;
    EGLSurface      eglSurf;
    EGLContext      eglCtx;
#else
    GLFWwindow*     window;
#endif
} GlDrawTextContext;

#define OFFSET(x) offsetof(GlDrawTextContext, x)

#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM
static const AVOption gldrawtext_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"fontfile",    "set font file",        OFFSET(fontfile),           AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {"text",        "set text",             OFFSET(text),               AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {"textfile",    "set text file",        OFFSET(textfile),           AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {"fontcolor",   "set foreground color", OFFSET(fontcolor.rgba),     AV_OPT_TYPE_COLOR,  {.str="black"}, 0, 0, FLAGS},
    {"fontcolor_expr", "set foreground color expression", OFFSET(fontcolor_expr), AV_OPT_TYPE_STRING, {.str=""}, 0, 0, FLAGS},
    {"bordercolor", "set border color",     OFFSET(bordercolor.rgba),   AV_OPT_TYPE_COLOR,  {.str="black"}, 0, 0, FLAGS},
    {"line_spacing",  "set line spacing in pixels", OFFSET(line_spacing),   AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN,  INT_MAX,FLAGS},
    {"fontsize",    "set font size",        OFFSET(fontsize_expr),      AV_OPT_TYPE_STRING, {.str=NULL},  0, 0 , FLAGS},
    {"x",           "set x",     OFFSET(x),             AV_OPT_TYPE_INT, {.i64= 0 },   0, 4096, FLAGS},
    {"y",           "set y",     OFFSET(y),             AV_OPT_TYPE_INT, {.i64= 0 },   0, 4096, FLAGS},
    {"borderw",     "set border width",     OFFSET(borderw),            AV_OPT_TYPE_INT,    {.i64=0},     INT_MIN,  INT_MAX , FLAGS},
    {"tabsize",     "set tab size",         OFFSET(tabsize),            AV_OPT_TYPE_INT,    {.i64=4},     0,        INT_MAX , FLAGS},
#if CONFIG_LIBFONTCONFIG
    {"font",        "Font name",            OFFSET(font),               AV_OPT_TYPE_STRING, { .str = "Sans" },           .flags = FLAGS },
#endif
    {"reload",     "reload text file for each frame",                       OFFSET(reload),     AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"alpha",       "apply alpha while rendering", OFFSET(alpha),      AV_OPT_TYPE_INT, {.i64 = 255}, 0, 255, FLAGS},
    {"alphachannel", "keep alpha channel", OFFSET(alphachannel), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS},
    {"texture",     "set texture",         OFFSET(texture_filepath),       AV_OPT_TYPE_STRING, {.str=NULL},  0, 0, FLAGS},
    {NULL}};

AVFILTER_DEFINE_CLASS(gldrawtext);

static int load_font_file(AVFilterContext *ctx, const char *path, int index)
{
    GlDrawTextContext *s = ctx->priv;
    int err;

    err = FT_New_Face(s->library, path, index, &s->face);
    if (err) {
#if !CONFIG_LIBFONTCONFIG
        av_log(ctx, AV_LOG_ERROR, "Could not load font \"%s\": %s\n",
               s->fontfile, FT_ERRMSG(err));
#endif
        return AVERROR(EINVAL);
    }
    return 0;
}

#if CONFIG_LIBFONTCONFIG
static int load_font_fontconfig(AVFilterContext *ctx)
{
    GlDrawTextContext *s = ctx->priv;
    FcConfig *fontconfig;
    FcPattern *pat, *best;
    FcResult result = FcResultMatch;
    FcChar8 *filename;
    int index;
    double size;
    int err = AVERROR(ENOENT);
    int parse_err;

    fontconfig = FcInitLoadConfigAndFonts();
    if (!fontconfig) {
        av_log(ctx, AV_LOG_ERROR, "impossible to init fontconfig\n");
        return AVERROR_UNKNOWN;
    }
    pat = FcNameParse(s->fontfile ? s->fontfile :
                          (uint8_t *)(intptr_t)"default");
    if (!pat) {
        av_log(ctx, AV_LOG_ERROR, "could not parse fontconfig pat");
        return AVERROR(EINVAL);
    }

    FcPatternAddString(pat, FC_FAMILY, s->font);

    FcPatternAddDouble(pat, FC_SIZE, s->fontsize);

    FcDefaultSubstitute(pat);

    if (!FcConfigSubstitute(fontconfig, pat, FcMatchPattern)) {
        av_log(ctx, AV_LOG_ERROR, "could not substitue fontconfig options"); /* very unlikely */
        FcPatternDestroy(pat);
        return AVERROR(ENOMEM);
    }

    best = FcFontMatch(fontconfig, pat, &result);
    FcPatternDestroy(pat);

    if (!best || result != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR,
               "Cannot find a valid font for the family %s\n",
               s->font);
        goto fail;
    }

    if (
        FcPatternGetInteger(best, FC_INDEX, 0, &index   ) != FcResultMatch ||
        FcPatternGetDouble (best, FC_SIZE,  0, &size    ) != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR, "impossible to find font information");
        return AVERROR(EINVAL);
    }

    if (FcPatternGetString(best, FC_FILE, 0, &filename) != FcResultMatch) {
        av_log(ctx, AV_LOG_ERROR, "No file path for %s\n",
               s->font);
        goto fail;
    }

    av_log(ctx, AV_LOG_INFO, "Using \"%s\"\n", filename);
    if (parse_err)
        s->default_fontsize = size + 0.5;

    err = load_font_file(ctx, filename, index);
    if (err)
        return err;
    FcConfigDestroy(fontconfig);
fail:
    FcPatternDestroy(best);
    return err;
}
#endif

static int load_font(AVFilterContext *ctx)
{
    GlDrawTextContext *s = ctx->priv;
    int err;

    /* load the face, and set up the encoding, which is by default UTF-8 */
    err = load_font_file(ctx, s->fontfile, 0);
    if (!err)
        return 0;
#if CONFIG_LIBFONTCONFIG
    err = load_font_fontconfig(ctx);
    if (!err)
        return 0;
#endif
    return err;
}

static int load_textfile(AVFilterContext *ctx)
{
    GlDrawTextContext *s = ctx->priv;
    int err;
    uint8_t *textbuf;
    uint8_t *tmp;
    size_t textbuf_size;

    if ((err = av_file_map(s->textfile, &textbuf, &textbuf_size, 0, ctx)) < 0) {
        av_log(ctx, AV_LOG_ERROR,
               "The text file '%s' could not be read or is empty\n",
               s->textfile);
        return err;
    }

    if (textbuf_size > SIZE_MAX - 1 || !(tmp = av_realloc(s->text, textbuf_size + 1))) {
        av_file_unmap(textbuf, textbuf_size);
        return AVERROR(ENOMEM);
    }
    s->text = tmp;
    memcpy(s->text, textbuf, textbuf_size);
    s->text[textbuf_size] = 0;
    av_file_unmap(textbuf, textbuf_size);

    return 0;
}

static inline int is_newline(uint32_t c)
{
    return c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static int glyph_cmp(const void *key, const void *b)
{
    const Glyph *a = key, *bb = b;
    int64_t diff = (int64_t)a->code - (int64_t)bb->code;

    if (diff != 0)
         return diff > 0 ? 1 : -1;
    else
         return FFDIFFSIGN((int64_t)a->fontsize, (int64_t)bb->fontsize);
}

/**
 * Load glyphs corresponding to the UTF-32 codepoint code.
 */
static int load_glyph(AVFilterContext *ctx, Glyph **glyph_ptr, uint32_t code)
{
    GlDrawTextContext *s = ctx->priv;
    FT_BitmapGlyph bitmapglyph;
    Glyph *glyph;
    struct AVTreeNode *node = NULL;
    int ret;

    /* load glyph into s->face->glyph */
    if (FT_Load_Char(s->face, code, s->ft_load_flags))
        return AVERROR(EINVAL);

    glyph = av_mallocz(sizeof(*glyph));
    if (!glyph) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    glyph->code  = code;
    glyph->fontsize = s->fontsize;

    if (FT_Get_Glyph(s->face->glyph, &glyph->glyph)) {
        ret = AVERROR(EINVAL);
        goto error;
    }
    if (s->borderw) {
        glyph->border_glyph = glyph->glyph;
        if (FT_Glyph_StrokeBorder(&glyph->border_glyph, s->stroker, 0, 0) ||
            FT_Glyph_To_Bitmap(&glyph->border_glyph, FT_RENDER_MODE_NORMAL, 0, 1)) {
            ret = AVERROR_EXTERNAL;
            goto error;
        }
        bitmapglyph = (FT_BitmapGlyph) glyph->border_glyph;
        glyph->border_bitmap = bitmapglyph->bitmap;
    }
    if (FT_Glyph_To_Bitmap(&glyph->glyph, FT_RENDER_MODE_NORMAL, 0, 1)) {
        ret = AVERROR_EXTERNAL;
        goto error;
    }
    bitmapglyph = (FT_BitmapGlyph) glyph->glyph;

    glyph->bitmap      = bitmapglyph->bitmap;
    glyph->bitmap_left = bitmapglyph->left;
    glyph->bitmap_top  = bitmapglyph->top;
    glyph->advance     = s->face->glyph->advance.x >> 6;

    /* measure text height to calculate text_height (or the maximum text height) */
    FT_Glyph_Get_CBox(glyph->glyph, ft_glyph_bbox_pixels, &glyph->bbox);

    /* cache the newly created glyph */
    if (!(node = av_tree_node_alloc())) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    av_tree_insert(&s->glyphs, glyph, glyph_cmp, &node);

    if (glyph_ptr)
        *glyph_ptr = glyph;
    return 0;

error:
    if (glyph)
        av_freep(&glyph->glyph);

    av_freep(&glyph);
    av_freep(&node);
    return ret;
}

static av_cold int set_fontsize(AVFilterContext *ctx, unsigned int fontsize)
{
    int err;
    GlDrawTextContext *s = ctx->priv;

    if ((err = FT_Set_Pixel_Sizes(s->face, 0, fontsize))) {
        av_log(ctx, AV_LOG_ERROR, "Could not set font size to %d pixels: %s\n",
               fontsize, FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    s->fontsize = fontsize;

    return 0;
}

static av_cold int parse_fontsize(AVFilterContext *ctx)
{
    GlDrawTextContext *s = ctx->priv;
    int err;

    if (s->fontsize_pexpr)
        return 0;

    if (s->fontsize_expr == NULL)
        return AVERROR(EINVAL);

    if ((err = av_expr_parse(&s->fontsize_pexpr, s->fontsize_expr, var_names,
                             NULL, NULL, fun2_names, fun2, 0, ctx)) < 0)
        return err;

    return 0;
}

static av_cold int update_fontsize(AVFilterContext *ctx)
{
    GlDrawTextContext *s = ctx->priv;
    unsigned int fontsize = s->default_fontsize;
    int err;
    double size, roundedsize;

    // if no fontsize specified use the default
    if (s->fontsize_expr != NULL) {
        if ((err = parse_fontsize(ctx)) < 0)
           return err;

        size = av_expr_eval(s->fontsize_pexpr, s->var_values, &s->prng);

        if (!isnan(size)) {
            roundedsize = round(size);
            // test for overflow before cast
            if (!(roundedsize > INT_MIN && roundedsize < INT_MAX)) {
                av_log(ctx, AV_LOG_ERROR, "fontsize overflow\n");
                return AVERROR(EINVAL);
            }

            fontsize = roundedsize;
        }
    }

    if (fontsize == 0)
        fontsize = 1;

    // no change
    if (fontsize == s->fontsize)
        return 0;

    return set_fontsize(ctx, fontsize);
}

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

static void vbo_setup(GlDrawTextContext *gs)
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
    GlDrawTextContext *gs = ctx->priv;

    glGenTextures(1, &gs->frame_tex);
    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

    glUniform1i(glGetUniformLocation(gs->program, "tex"), 0);

    if (gs->texture_filepath) {
        int ret;
        AVFrame *tex_frame = av_frame_alloc();
        if (!tex_frame)
            return AVERROR(ENOMEM);

        if ((ret = ff_load_image(tex_frame->data, tex_frame->linesize,
                                &tex_frame->width, &tex_frame->height,
                                &tex_frame->format, gs->texture_filepath, gs)) < 0)
        {
            av_log(ctx, AV_LOG_ERROR, "failed to load texture file: %s\n", gs->texture_filepath);
            return ret;
        }

        if (tex_frame->format != AV_PIX_FMT_RGB24 && tex_frame->format != AV_PIX_FMT_RGBA && 
            tex_frame->format != AV_PIX_FMT_GRAY8) {
            av_log(ctx, AV_LOG_ERROR, "texture image is not a rgb image: %d(%s)\n", 
                tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
            return AVERROR(EINVAL);
        }

        int width = tex_frame->width;
        int height = tex_frame->height;
        int channels;
        int pix_fmt;
        switch (tex_frame->format) {
        case AV_PIX_FMT_RGB24:
            channels = 3;
            pix_fmt = GL_RGB;
            break;
        case AV_PIX_FMT_RGBA:
            channels = 4;
            pix_fmt = GL_RGBA;
            break;
        case AV_PIX_FMT_GRAY8:
            channels = 1;
            pix_fmt = GL_RED;
            break;
        default:
            av_log(ctx, AV_LOG_ERROR, "unsupported pix format: %d(%s)\n", 
                tex_frame->format, av_get_pix_fmt_name(tex_frame->format));
            return AVERROR(EINVAL);
        }
        int frame_data_size = width * height * channels;
        if (!gs->tex_data)
            gs->tex_data = av_mallocz(frame_data_size);
        
        if (tex_frame->linesize[0] == width * channels) {
            // bunch copy
            memcpy(gs->tex_data, tex_frame->data[0], tex_frame->linesize[0] * height);
        }
        else {
            // line copy
            int data_offset = 0;
            int frame_offset = 0;
            for (int line=0;line<height;line++) { 
                memcpy(gs->tex_data, tex_frame->data[0] + frame_offset, width * channels);
                data_offset += width * channels;
                frame_offset += tex_frame->linesize[0];
            }
        }

        av_frame_free(&tex_frame);

        glGenTextures(1, &gs->extra_tex);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, gs->extra_tex);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, pix_fmt, width, height, 0, pix_fmt, GL_UNSIGNED_BYTE, gs->tex_data);

        glUniform1i(glGetUniformLocation(gs->program, "text_tex"), 1);
    }

    return 0;
}

static int build_program(AVFilterContext *ctx, GLuint *program, const char* vs, const char* fs)
{
    GLuint v_shader, f_shader;
    GlDrawTextContext *gs = ctx->priv;

    if (!((v_shader = build_shader(ctx, vs, GL_VERTEX_SHADER)) &&
          (f_shader = build_shader(ctx, fs, GL_FRAGMENT_SHADER))))
    {
        return -1;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, v_shader);
    glAttachShader(prog, f_shader);
    glLinkProgram(prog);

    GLint status;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (status == GL_TRUE) {
        *program = prog;
        return 0;
    }

    return -1;
}

static av_cold int init(AVFilterContext *ctx)
{
    int err;
    GlDrawTextContext *gs = ctx->priv;
    
#ifndef GL_TRANSITION_USING_EGL
    if (gs->no_window)
    {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    if(!glfwInit())
        return -1;
#endif

    GlDrawTextContext *s = ctx->priv;
    Glyph *glyph;

    s->fontsize = 0;
    s->default_fontsize = 16;

    if (!s->fontfile && !CONFIG_LIBFONTCONFIG) {
        av_log(ctx, AV_LOG_ERROR, "No font filename provided\n");
        return AVERROR(EINVAL);
    }

    if (s->textfile) {
        if (s->text) {
            av_log(ctx, AV_LOG_ERROR,
                   "Both text and text file provided. Please provide only one\n");
            return AVERROR(EINVAL);
        }
        if ((err = load_textfile(ctx)) < 0)
            return err;
    }

    if (s->reload && !s->textfile)
        av_log(ctx, AV_LOG_WARNING, "No file to reload\n");

    if (!s->text) {
        av_log(ctx, AV_LOG_ERROR,
               "Either text, a valid file or a timecode must be provided\n");
        return AVERROR(EINVAL);
    }

    if ((err = FT_Init_FreeType(&(s->library)))) {
        av_log(ctx, AV_LOG_ERROR,
               "Could not load FreeType: %s\n", FT_ERRMSG(err));
        return AVERROR(EINVAL);
    }

    if ((err = load_font(ctx)) < 0)
        return err;

    if ((err = update_fontsize(ctx)) < 0)
        return err;

    if (s->borderw) {
        if (FT_Stroker_New(s->library, &s->stroker)) {
            av_log(ctx, AV_LOG_ERROR, "Coult not init FT stroker\n");
            return AVERROR_EXTERNAL;
        }
        FT_Stroker_Set(s->stroker, s->borderw << 6, FT_STROKER_LINECAP_ROUND,
                       FT_STROKER_LINEJOIN_ROUND, 0);
    }

    /* load the fallback glyph with code 0 */
    load_glyph(ctx, NULL, 0);

    /* set the tabsize in pixels */
    if ((err = load_glyph(ctx, &glyph, ' ')) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Could not set tabsize.\n");
        return err;
    }
    s->tabsize *= glyph->advance;

    av_bprint_init(&s->expanded_fontcolor, 0, AV_BPRINT_SIZE_UNLIMITED);

    return 0;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    AVFilterContext *ctx = fromLink->dst;
    GlDrawTextContext *gs = ctx->priv;

    gs->time = glGetUniformLocation(gs->program, "u_Time");
    glUniform1i(gs->time, 0.0f);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlDrawTextContext *gs = ctx->priv;

    ff_draw_init(&gs->dc, inlink->format, FF_DRAW_PROCESS_ALPHA);
    ff_draw_color(&gs->dc, &gs->fontcolor,   gs->fontcolor.rgba);
    ff_draw_color(&gs->dc, &gs->shadowcolor, gs->shadowcolor.rgba);
    ff_draw_color(&gs->dc, &gs->bordercolor, gs->bordercolor.rgba);
    ff_draw_color(&gs->dc, &gs->boxcolor,    gs->boxcolor.rgba);

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
    if ((ret = build_program(ctx, &gs->program, v_shader_source, f_shader_source)) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to build ogl program: %d\n", ret);
        return ret;
    }

    glUseProgram(gs->program);
    vbo_setup(gs);
    setup_uniforms(inlink);
    if (tex_setup(inlink) < 0)
        return -1;

    return 0;
}

static int draw_glyphs(GlDrawTextContext *s, AVFrame *frame,
                       int width, int height,
                       FFDrawColor *color,
                       int x, int y, int borderw,
                       float time)
{
    char *text = s->text;
    uint32_t code = 0;
    int i, x1, y1;
    uint8_t *p;
    Glyph *glyph = NULL;

    for (i = 0, p = text; *p; i++) {
        FT_Bitmap bitmap;
        Glyph dummy = { 0 };
        GET_UTF8(code, *p ? *p++ : 0, code = 0xfffd; goto continue_on_invalid;);
continue_on_invalid:

        /* skip new line chars, just go to new line */
        if (code == '\n' || code == '\r' || code == '\t')
            continue;

        dummy.code = code;
        dummy.fontsize = s->fontsize;
        glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);

        bitmap = borderw ? glyph->border_bitmap : glyph->bitmap;

        if (glyph->bitmap.pixel_mode != FT_PIXEL_MODE_MONO &&
            glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
            return AVERROR(EINVAL);

        x1 = s->positions[i].x+s->x+x - borderw;
        y1 = s->positions[i].y+s->y+y - borderw;
        //float gap = 5.0 / strlen(s->text);
        //int idx = (int)(time / gap);
        //if (time < 5.0 && i == idx)
        //    y1 += (-100 * (time - gap * idx) / 2.0);

        //x1 += (rand() % 20 - 10);
        //y1 += (rand() % 20 - 10);

        ff_blend_mask(&s->dc, color,
                      frame->data, frame->linesize, width, height,
                      bitmap.buffer, bitmap.pitch,
                      bitmap.width, bitmap.rows,
                      bitmap.pixel_mode == FT_PIXEL_MODE_MONO ? 0 : 3,
                      0, x1, y1);
    }

    return 0;
}

static void update_color_with_alpha(GlDrawTextContext *s, FFDrawColor *color, const FFDrawColor incolor)
{
    *color = incolor;
    color->rgba[3] = (color->rgba[3] * s->alpha) / 255;
    ff_draw_color(&s->dc, color, color->rgba);
}

static int draw_text(AVFilterContext *ctx, AVFrame *frame,
                     int width, int height,
                     float time)
{
    GlDrawTextContext *s = ctx->priv;

    uint32_t code = 0, prev_code = 0;
    int x = 0, y = 0, i = 0, ret;
    int max_text_line_w = 0, len;
    int box_w, box_h;
    char *text;
    uint8_t *p;
    int y_min = 32000, y_max = -32000;
    int x_min = 32000, x_max = -32000;
    FT_Vector delta;
    Glyph *glyph = NULL, *prev_glyph = NULL;
    Glyph dummy = { 0 }; 

    FFDrawColor fontcolor;
    FFDrawColor shadowcolor;
    FFDrawColor bordercolor;
    FFDrawColor boxcolor; 

    text = s->text;

    if ((len = strlen(s->text)) > s->nb_positions) {
        av_log(NULL, AV_LOG_ERROR, "av_realloc: len %d, size %ld\n", len, len*sizeof(*s->positions));
        if (!(s->positions =
              av_realloc(s->positions, len*sizeof(*s->positions))))
            return AVERROR(ENOMEM);
        s->nb_positions = len;
    }

    if (s->fontcolor_expr[0]) {
        /* If expression is set, evaluate and replace the static value */
        av_bprint_clear(&s->expanded_fontcolor);
        if (!av_bprint_is_complete(&s->expanded_fontcolor))
            return AVERROR(ENOMEM);
        av_log(s, AV_LOG_DEBUG, "Evaluated fontcolor is '%s'\n", s->expanded_fontcolor.str);
        ret = av_parse_color(s->fontcolor.rgba, s->expanded_fontcolor.str, -1, s);
        if (ret)
            return ret;
        ff_draw_color(&s->dc, &s->fontcolor, s->fontcolor.rgba);
    }

    x = 0;
    y = 0;

    if ((ret = update_fontsize(ctx)) < 0)
        return ret;

    /* load and cache glyphs */
    for (i = 0, p = text; *p; i++) {
        GET_UTF8(code, *p ? *p++ : 0, code = 0xfffd; goto continue_on_invalid;);
continue_on_invalid:

        /* get glyph */
        dummy.code = code;
        dummy.fontsize = s->fontsize;
        glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);
        if (!glyph) {
            ret = load_glyph(ctx, &glyph, code);
            if (ret < 0)
                return ret;
        }

        y_min = FFMIN(glyph->bbox.yMin, y_min);
        y_max = FFMAX(glyph->bbox.yMax, y_max);
        x_min = FFMIN(glyph->bbox.xMin, x_min);
        x_max = FFMAX(glyph->bbox.xMax, x_max);
    }
    s->max_glyph_h = y_max - y_min;
    s->max_glyph_w = x_max - x_min;

    /* compute and save position for each glyph */
    glyph = NULL;
    for (i = 0, p = text; *p; i++) {
        /*
        if (time < 5.0) {
            int err;
            if ((err = FT_Set_Pixel_Sizes(s->face, 0, s->fontsize + 100))) {
                av_log(ctx, AV_LOG_ERROR, "Could not set font size to %d pixels: %s\n",
                    s->fontsize + 20, FT_ERRMSG(err));
                return AVERROR(EINVAL);
            }
        }
        else {
            FT_Set_Pixel_Sizes(s->face, 0, s->fontsize);
        }
        */

        GET_UTF8(code, *p ? *p++ : 0, code = 0xfffd; goto continue_on_invalid2;);
continue_on_invalid2:

        /* skip the \n in the sequence \r\n */
        if (prev_code == '\r' && code == '\n')
            continue;

        prev_code = code;
        if (is_newline(code)) {

            max_text_line_w = FFMAX(max_text_line_w, x);
            y += s->max_glyph_h + s->line_spacing;
            x = 0;
            continue;
        }

        /* get glyph */
        prev_glyph = glyph;
        dummy.code = code;
        dummy.fontsize = s->fontsize;
        glyph = av_tree_find(s->glyphs, &dummy, glyph_cmp, NULL);

        /* save position */
        s->positions[i].x = x + glyph->bitmap_left;
        s->positions[i].y = y - glyph->bitmap_top + y_max;
        if (code == '\t') x  = (x / s->tabsize + 1)*s->tabsize;
        else              x += glyph->advance;
    }

    max_text_line_w = FFMAX(x, max_text_line_w); 

    update_color_with_alpha(s, &fontcolor  , s->fontcolor  );
    update_color_with_alpha(s, &shadowcolor, s->shadowcolor);
    update_color_with_alpha(s, &bordercolor, s->bordercolor);
    update_color_with_alpha(s, &boxcolor   , s->boxcolor   );

    box_w = max_text_line_w;
    box_h = y + s->max_glyph_h;

    static int once = 1;
    if (once) {
        av_log(NULL, AV_LOG_ERROR, "s->fontcolor: %d %d %d %d\n", s->fontcolor.rgba[0], s->fontcolor.rgba[1], s->fontcolor.rgba[2], s->fontcolor.rgba[3]);
        av_log(NULL, AV_LOG_ERROR, "fontcolor: %d %d %d %d\n", fontcolor.rgba[0], fontcolor.rgba[1], fontcolor.rgba[2], fontcolor.rgba[3]);
        av_log(NULL, AV_LOG_ERROR, "x: %d, y: %d, box_w: %d, box_h: %d\n", s->x, s->y, box_w, box_h);

        once = 0;
    }

    if (s->borderw) {
        if ((ret = draw_glyphs(s, frame, width, height,
                               &bordercolor, 0, 0, s->borderw, time)) < 0)
            return ret;
    }
    if ((ret = draw_glyphs(s, frame, width, height,
                           &fontcolor, 0, 0, 0, time)) < 0)
        return ret;

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlDrawTextContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    //glDrawArrays(GL_TRIANGLES, 0, 6);

    const float time = in->pts * av_q2d(inlink->time_base);
    glUniform1f(gs->time, time);
    
    //glUniform1f(glGetUniformLocation(gs->program, "deg"), time / 5.0);
    
    draw_text(ctx, in, inlink->w, inlink->h, time);

    // 1,0 -> 0,0
    float v = time / 5.0f;
    if (v > 1.0)
        v = 1.0;
    //glUniform2f(glGetUniformLocation(gs->program, "eraseUV"), v, 0.0f);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, in->linesize[0] / gs->channel_num);

    // render it!!!
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gs->frame_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, gs->pix_fmt, inlink->w, inlink->h, 0, gs->pix_fmt, 
        GL_UNSIGNED_BYTE, in->data[0]);
    
    if (gs->texture_filepath) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gs->extra_tex);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glReadPixels(0, 0, outlink->w, outlink->h, gs->pix_fmt, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int glyph_enu_free(void *opaque, void *elem)
{
    Glyph *glyph = elem;

    FT_Done_Glyph(glyph->glyph);
    FT_Done_Glyph(glyph->border_glyph);
    av_free(elem);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlDrawTextContext *gs = ctx->priv;

    av_freep(&gs->positions);
    gs->nb_positions = 0;

    av_tree_enumerate(gs->glyphs, NULL, NULL, glyph_enu_free);
    av_tree_destroy(gs->glyphs);
    gs->glyphs = NULL;

    FT_Done_Face(gs->face);
    FT_Stroker_Done(gs->stroker);
    FT_Done_FreeType(gs->library);

    av_bprint_finalize(&gs->expanded_fontcolor, NULL);

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
}

static int query_formats(AVFilterContext *ctx)
{
    //static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    //return ff_set_common_formats(ctx, ff_make_format_list(formats));

    GlDrawTextContext *c = ctx->priv;
    static const enum AVPixelFormat pix_fmts_rgb[] = {
        AV_PIX_FMT_RGB24,    AV_PIX_FMT_BGR24,
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    static const enum AVPixelFormat pix_fmts_rgba[] = {
        AV_PIX_FMT_ARGB,     AV_PIX_FMT_ABGR,
        AV_PIX_FMT_RGBA,     AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NONE
    };
    AVFilterFormats *fmts_list;

    if (c->alphachannel) {
        c->pix_fmt = GL_RGBA;
        c->channel_num = 4;
        fmts_list = ff_make_format_list(pix_fmts_rgba);
    } else {
        c->pix_fmt = GL_RGB;
        c->channel_num = 3;
        fmts_list = ff_make_format_list(pix_fmts_rgb);
    }
    if (!fmts_list) {
      return AVERROR(ENOMEM);
    }
    return ff_set_common_formats(ctx, fmts_list);
}

static const AVFilterPad gldrawtext_inputs[] = {
    {.name = "default",
     .type = AVMEDIA_TYPE_VIDEO,
     .config_props = config_props,
     .filter_frame = filter_frame},
    {NULL}};

static const AVFilterPad gldrawtext_outputs[] = {
    {.name = "default", .type = AVMEDIA_TYPE_VIDEO}, {NULL}};

AVFilter ff_vf_gldrawtext = {
    .name = "gldrawtext",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter drawtext"),
    .priv_size = sizeof(GlDrawTextContext),
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .inputs = gldrawtext_inputs,
    .outputs = gldrawtext_outputs,
    .priv_class = &gldrawtext_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC};
