#include "libavutil/opt.h"
#include "internal.h"
#include "framesync.h"
#include "glutil.h"

#include <string.h>

static char *strsep(char **stringp, const char *delim) {
    char *rv = *stringp;
    if (rv) {
        *stringp += strcspn(*stringp, delim);
        if (**stringp)
            *(*stringp)++ = '\0';
        else
            *stringp = 0; }
    return rv;
}

// refer to https://github.com/transitive-bullshit/ffmpeg-gl-transition
// example:
// ./ffmpeg_g -ss 60 -i ~/work/media/astroboy.mp4 -i ~/work/media/TimeCode.mov 
//   -filter_complex "[0:v]scale=640:480[v0];[1:v]scale=640:480[v1];
//   [v0][v1]gltransition=duration=0.5:offset=5:source=crosswarp.glsl:source=direc.glsl:uniforms='direction=vec2(0.0,1.0)'" 
//   -c:v libx264 -b:v 2000k -c:a copy -t 10 -y out.mp4

// gltransition=duration=0.5:offset=3:source=wd.glsl:uniforms='amplitude=30.0&speed=30.0"

// https://gl-transitions.com/gallery

#include <float.h> // for DBL_MAX

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#endif

#include <GLFW/glfw3.h>

#define FROM (0)
#define TO (1)

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
    //"#version 130\n"
    "attribute vec2 position;\n"
    "varying vec2 texCoord;\n"
    "void main(void) {\n"
    "  gl_Position = vec4(position, 0, 1);\n"
    "  vec2 _uv = position * 0.5 + 0.5;\n"
    "  texCoord = vec2(_uv.x, 1.0 - _uv.y);\n"
    "}\n";

static const GLchar *f_shader_template =
    "varying vec2 texCoord;\n"
    "uniform sampler2D from;\n"
    "uniform sampler2D to;\n"
    "uniform float progress;\n"
    "uniform float ratio;\n"
    "uniform float _fromR;\n"
    "uniform float _toR;\n"
    "\n"
    "vec4 getFromColor(vec2 uv) {\n"
    "  return texture2D(from, vec2(uv.x, 1.0 - uv.y));\n"
    "}\n"
    "\n"
    "vec4 getToColor(vec2 uv) {\n"
    "  return texture2D(to, vec2(uv.x, 1.0 - uv.y));\n"
    "}\n"
    "\n"
    "\n%s\n"
    "void main() {\n"
    "  gl_FragColor = transition(texCoord);\n"
    "}\n";

static const GLchar *f_default_transition_source =
    "vec4 transition (vec2 uv) {\n"
    "  return mix(\n"
    "    getFromColor(uv),\n"
    "    getToColor(uv),\n"
    "    progress\n"
    "  );\n"
    "}\n";

#define PIXEL_FORMAT GL_RGB

typedef struct
{
    const AVClass *class;
    FFFrameSync fs;

    // input options
    double duration;
    double offset;
    char *source;
    char *uniforms;

    // timestamp of the first frame in the output, in the timebase units
    int64_t first_pts;

    // uniforms
    GLuint from;
    GLuint to;
    GLint progress;
    GLint ratio;
    GLint _fromR;
    GLint _toR;

    GLuint program;
    GLFWwindow *window;
    GLuint pos_buf;

    int no_window;

    GLchar *f_shader_source;
} GlTransitionContext;

#define OFFSET(x) offsetof(GlTransitionContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM

static const AVOption gltransition_options[] = {
    {"nowindow", "ssh mode, no window init open gl context", OFFSET(no_window), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, .flags = FLAGS},
    {"duration", "transition duration in seconds", OFFSET(duration), AV_OPT_TYPE_DOUBLE, {.dbl = 1.0}, 0, DBL_MAX, FLAGS},
    {"offset", "delay before startingtransition in seconds", OFFSET(offset), AV_OPT_TYPE_DOUBLE, {.dbl = 0.0}, 0, DBL_MAX, FLAGS},
    {"source", "path to the gl-transition source file (defaults to basic fade)", OFFSET(source), AV_OPT_TYPE_STRING, {.str = NULL}, CHAR_MIN, CHAR_MAX, FLAGS},
    { "uniforms", "uniform vars setting, e.g. uniforms='some_var=1.0&other_var=1'", OFFSET(uniforms), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS },
    {NULL}};

FRAMESYNC_DEFINE_CLASS(gltransition, GlTransitionContext, fs);

typedef struct {
  char **strings;
  int len;
} StringArray_t;

static StringArray_t parseQueryString (const char *str_query)  {
  StringArray_t sa;
  int offset = 0;
  int offset_seg = 0;
  int cursor_start = 0;
  int cursor_end= 0;
  int cnt_seg = 1;
  auto len = (int) (strlen(str_query));

  for (int i = 0; i < len; i++) {
    if (str_query[i] == '=' || str_query[i] == '&') {
      cnt_seg++;
    }
  }
  char **strings = (char **) av_malloc(sizeof(char *) * cnt_seg);
  sa.len = cnt_seg;

  for (; offset <= len; offset++) {
    if (str_query[offset] == '=' || str_query[offset] == '&' || offset == len) {
      int word_len = cursor_end - cursor_start;
      char *seg = (char *) malloc(sizeof(char) * (word_len+ 1));
      strncpy(seg, str_query+cursor_start, (size_t)(word_len));
      seg[word_len] = '\0';
      strings [offset_seg] = seg;
      offset_seg++;
      cursor_end++;
      cursor_start = cursor_end;
    } else {
      cursor_end++;
    }
  }
  sa.strings = strings ;
  return sa;
}

static int strToInt(char *to_convert, int *i) {
  char *p = to_convert;
  errno = 0;
  *i = (int) strtol(to_convert, &p, 10);
  if (errno != 0 || to_convert == p || *p != 0) {
    return 0;
  }
  return 1;
}

static int strToFloat(char *to_convert, float *f) {
  char *p = to_convert;
  errno = 0;
  *f = strtof(to_convert, &p);
  if (errno != 0 || to_convert == p || *p != 0) {
    return 0;
  }
  return 1;
}

#define STRING_SPLIT_DEFINE(type, Type) \
static void strSplit##Type(const char *strLiteral, const char *delimiter, type *pOutputs, int len) { \
  char *token, *str, *tofree;                                                           \
  int offset = 0;                                                                       \
  tofree = str = strdup(strLiteral);                                                    \
  while ((token = strsep(&str, delimiter)) && offset < len) {                           \
    type t;                                                                             \
    if (!strTo##Type(token, &t)) {                                                      \
      t = (type)0;                                                                      \
    }                                                                                   \
    pOutputs[offset] = t;                                                                \
    offset++;                                                                           \
  }                                                                                     \
  free(tofree);                                                                         \
}

STRING_SPLIT_DEFINE(int, Int)
STRING_SPLIT_DEFINE(float, Float)

//assume that the string is compact
#define PARSE_GLSL_VECTOR(glType, typeLen, type, Type)                      \
static int parseGlSL##Type##Vector(const char* str,type *pOutput,int *pShape){      \
  int len = (int)strlen(str);                                               \
  int bodyPos = typeLen + 2;                                                \
  if (memcmp(str,glType, typeLen) != 0) {                                   \
    return 0;                                                               \
  }                                                                         \
  *pShape = (int) (str[typeLen]  - '0');                                    \
  int bodyLen = len - bodyPos;                                              \
  char *body = (char *) av_malloc(sizeof(char) * bodyLen);                     \
  strncpy(body, str + bodyPos, (size_t) bodyLen - 1);                       \
  body[bodyLen-1] = '\0';                                                   \
  strSplit##Type(body, ",", pOutput, *pShape);                               \
  av_free(body);                                                               \
  return 1;                                                                 \
}

PARSE_GLSL_VECTOR("vec", 3, float, Float)
PARSE_GLSL_VECTOR("ivec", 4, int, Int)

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

static void vbo_setup(GlTransitionContext *gs)
{
    glGenBuffers(1, &gs->pos_buf);
    glBindBuffer(GL_ARRAY_BUFFER, gs->pos_buf);
    glBufferData(GL_ARRAY_BUFFER, sizeof(position), position, GL_STATIC_DRAW);

    GLint loc = glGetAttribLocation(gs->program, "position");
    glEnableVertexAttribArray(loc);
    glVertexAttribPointer(loc, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void tex_setup(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlTransitionContext *gs = ctx->priv;

    { // from
        glGenTextures(1, &gs->from);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gs->from);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

        glUniform1i(glGetUniformLocation(gs->program, "from"), 0);
    }

    { // to
        glGenTextures(1, &gs->to);
        glActiveTexture(GL_TEXTURE0 + 1);
        glBindTexture(GL_TEXTURE_2D, gs->to);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, NULL);

        glUniform1i(glGetUniformLocation(gs->program, "to"), 1);
    }
}

static int build_program(AVFilterContext *ctx)
{
    GLuint v_shader, f_shader;
    GlTransitionContext *gs = ctx->priv;

    if (!(v_shader = build_shader(ctx, v_shader_source, GL_VERTEX_SHADER))) {
        av_log(ctx, AV_LOG_ERROR, "invalid vertex shader\n");
        return -1;
    }

    char *source = NULL;
    if (gs->source) {
        FILE *f = fopen(gs->source, "rb");

        if (!f)
        {
            av_log(ctx, AV_LOG_ERROR, "invalid transition source file \"%s\"\n", gs->source);
            return -1;
        }

        fseek(f, 0, SEEK_END);
        unsigned long fsize = ftell(f);
        fseek(f, 0, SEEK_SET);

        source = malloc(fsize + 1);
        fread(source, fsize, 1, f);
        fclose(f);

        source[fsize] = 0;
    }

    const char *transition_source = source ? source : f_default_transition_source;

    int len = strlen(f_shader_template) + strlen(transition_source);
    gs->f_shader_source = av_calloc(len, sizeof(*gs->f_shader_source));
    if (!gs->f_shader_source) {
        return AVERROR(ENOMEM);
    }

    snprintf(gs->f_shader_source, len * sizeof(*gs->f_shader_source), f_shader_template, transition_source);
    av_log(ctx, AV_LOG_ERROR, "\n%s\n", gs->f_shader_source);

    if (source) {
        free(source);
        source = NULL;
    }

    if (!(f_shader = build_shader(ctx, gs->f_shader_source, GL_FRAGMENT_SHADER)))
    {
        av_log(ctx, AV_LOG_ERROR, "invalid fragment shader\n");
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

static AVFrame *apply_transition(FFFrameSync *fs,
                                 AVFilterContext *ctx,
                                 AVFrame *fromFrame,
                                 const AVFrame *toFrame)
{
    GlTransitionContext *c = ctx->priv;
    AVFilterLink *fromLink = ctx->inputs[FROM];
    AVFilterLink *toLink = ctx->inputs[TO];
    AVFilterLink *outLink = ctx->outputs[0];
    AVFrame *outFrame;

    outFrame = ff_get_video_buffer(outLink, outLink->w, outLink->h);
    if (!outFrame)
    {
        return NULL;
    }

    av_frame_copy_props(outFrame, fromFrame);

#ifdef GL_TRANSITION_USING_EGL
    eglMakeCurrent(c->eglDpy, c->eglSurf, c->eglSurf, c->eglCtx);
#else
    glfwMakeContextCurrent(c->window);
#endif

    glUseProgram(c->program);

    const float ts = ((fs->pts - c->first_pts) / (float)fs->time_base.den) - c->offset;
    const float progress = FFMAX(0.0f, FFMIN(1.0f, ts / c->duration));
    // av_log(ctx, AV_LOG_ERROR, "transition '%s' %llu %f %f\n", c->source, fs->pts - c->first_pts, ts, progress);
    glUniform1f(c->progress, progress);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, c->from);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, fromFrame->linesize[0] / 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fromLink->w, fromLink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, fromFrame->data[0]);

    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, c->to);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, toFrame->linesize[0] / 3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, toLink->w, toLink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, toFrame->data[0]);

    glDrawArrays(GL_TRIANGLES, 0, 6);
    glPixelStorei(GL_PACK_ROW_LENGTH, outFrame->linesize[0] / 3);
    glReadPixels(0, 0, outLink->w, outLink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)outFrame->data[0]);

    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    av_frame_free(&fromFrame);

    return outFrame;
}

static int blend_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    GlTransitionContext *c = ctx->priv;

    AVFrame *fromFrame, *toFrame, *outFrame;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &fromFrame, &toFrame);
    if (ret < 0)
    {
        return ret;
    }

    if (c->first_pts == AV_NOPTS_VALUE && fromFrame && fromFrame->pts != AV_NOPTS_VALUE)
    {
        c->first_pts = fromFrame->pts;
    }

    if (!toFrame)
    {
        return ff_filter_frame(ctx->outputs[0], fromFrame);
    }

    outFrame = apply_transition(fs, ctx, fromFrame, toFrame);
    if (!outFrame)
    {
        return AVERROR(ENOMEM);
    }

    return ff_filter_frame(ctx->outputs[0], outFrame);
}

static av_cold int init(AVFilterContext *ctx)
{
    GlTransitionContext *gs = ctx->priv;
    gs->fs.on_event = blend_frame;
    gs->first_pts = AV_NOPTS_VALUE;

    if (gs->no_window) {
        av_log(NULL, AV_LOG_ERROR, "open gl no window init ON\n");
        no_window_init();
    }

    return glfwInit() ? 0 : -1;
}

static void setup_uniforms(AVFilterLink *fromLink)
{
    AVFilterContext *ctx = fromLink->dst;
    GlTransitionContext *gs = ctx->priv;

    gs->progress = glGetUniformLocation(gs->program, "progress");
    glUniform1f(gs->progress, 0.0f);

    // TODO: this should be output ratio
    gs->ratio = glGetUniformLocation(gs->program, "ratio");
    glUniform1f(gs->ratio, fromLink->w / (float)fromLink->h);

    gs->_fromR = glGetUniformLocation(gs->program, "_fromR");
    glUniform1f(gs->_fromR, fromLink->w / (float)fromLink->h);

    // TODO: initialize this in config_props for "to" input
    gs->_toR = glGetUniformLocation(gs->program, "_toR");
    glUniform1f(gs->_toR, fromLink->w / (float)fromLink->h);

    if(gs->uniforms) {
        StringArray_t sa = parseQueryString(gs->uniforms);
        if (sa.len > 0) {
            for (int i = 0;i<sa.len;i+=2) {
                GLint location = glGetUniformLocation(gs->program, sa.strings[i]);
                if (location >= 0) {
                    int intVar;
                    float floatVar;
                    int vecShape = 0;
                    int intVec[4] = {0};
                    float floatVec[4] = {0.0};
                    if (parseGlSLFloatVector(sa.strings[i + 1], floatVec, &vecShape)) {
                        switch (vecShape) {
                        case 2:
                            glUniform2f(location, floatVec[0], floatVec[1]);
                            break;
                        case 3:
                            glUniform3f(location, floatVec[0], floatVec[1], floatVec[2]);
                            break;
                        case 4:
                            glUniform4f(location, floatVec[0], floatVec[1], floatVec[2], floatVec[3]);
                            break;
                        default:
                            break;
                        }
                    } else if (parseGlSLIntVector(sa.strings[i + 1], intVec, &vecShape)) {
                        switch (vecShape) {
                        case 2:
                            glUniform2i(location, intVec[0], intVec[1]);
                            break;
                        case 3:
                            glUniform3i(location, intVec[0], intVec[1], intVec[2]);
                            break;
                        case 4:
                            glUniform4i(location, intVec[0], intVec[1], intVec[2], intVec[3]);
                        default:
                            break;
                        }
                    } else if (strToInt(sa.strings[i + 1], &intVar)) {
                        glUniform1i(location, intVar);
                    } else if (strToFloat(sa.strings[i + 1], &floatVar)) {
                        glUniform1f(location, floatVar);
                    } else {
                        av_log(ctx, AV_LOG_ERROR, "value %s not supported, supported: int float ivec vec)", sa.strings[i + 1]);
                    }
                } else {
                av_log(ctx, AV_LOG_ERROR, "can't get location of uniform: %s,fail set value: %s\n", sa.strings[i],
                        sa.strings[i + 1]);
                }
            } // for

            //free strings;
            for (int i = 0; i < sa.len; i++) {
                free(sa.strings[i]);
            }
        }
    }
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    GlTransitionContext *gs = ctx->priv;

    glfwWindowHint(GLFW_VISIBLE, 0);
    gs->window = glfwCreateWindow(inlink->w, inlink->h, "", NULL, NULL);

    glfwMakeContextCurrent(gs->window);

#ifndef __APPLE__
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
    tex_setup(inlink);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    GlTransitionContext *gs = ctx->priv;

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out)
    {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inlink->w, inlink->h, 0, PIXEL_FORMAT, GL_UNSIGNED_BYTE, in->data[0]);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glReadPixels(0, 0, outlink->w, outlink->h, PIXEL_FORMAT, GL_UNSIGNED_BYTE, (GLvoid *)out->data[0]);

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    GlTransitionContext *gs = ctx->priv;
    ff_framesync_uninit(&gs->fs);

    if (gs->window) {
        glDeleteTextures(1, &gs->from);
        glDeleteTextures(1, &gs->to);
        glDeleteProgram(gs->program);
        glDeleteBuffers(1, &gs->pos_buf);
        glfwDestroyWindow(gs->window);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat formats[] = {AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE};
    return ff_set_common_formats(ctx, ff_make_format_list(formats));
}

static int activate(AVFilterContext *ctx)
{
    GlTransitionContext *c = ctx->priv;
    return ff_framesync_activate(&c->fs);
}

static int config_output(AVFilterLink *outLink)
{
    AVFilterContext *ctx = outLink->src;
    GlTransitionContext *c = ctx->priv;
    AVFilterLink *fromLink = ctx->inputs[FROM];
    AVFilterLink *toLink = ctx->inputs[TO];
    int ret;

    if (fromLink->format != toLink->format)
    {
        av_log(ctx, AV_LOG_ERROR, "inputs must be of same pixel format\n");
        return AVERROR(EINVAL);
    }

    if (fromLink->w != toLink->w || fromLink->h != toLink->h)
    {
        av_log(ctx, AV_LOG_ERROR, "First input link %s parameters "
                                  "(size %dx%d) do not match the corresponding "
                                  "second input link %s parameters (size %dx%d)\n",
               ctx->input_pads[FROM].name, fromLink->w, fromLink->h,
               ctx->input_pads[TO].name, toLink->w, toLink->h);
        return AVERROR(EINVAL);
    }

    outLink->w = fromLink->w;
    outLink->h = fromLink->h;
    // outLink->time_base = fromLink->time_base;
    outLink->frame_rate = fromLink->frame_rate;

    if ((ret = ff_framesync_init_dualinput(&c->fs, ctx)) < 0)
    {
        return ret;
    }

    return ff_framesync_configure(&c->fs);
}

static const AVFilterPad gltransition_inputs[] = {
    {.name = "from",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props},
    {
        .name = "to",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    {NULL}};

static const AVFilterPad gltransition_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    {NULL}};

AVFilter ff_vf_gltransition = {
    .name = "gltransition",
    .description = NULL_IF_CONFIG_SMALL("OpenGL shader filter transition"),
    .priv_size = sizeof(GlTransitionContext),
    .preinit = gltransition_framesync_preinit,
    .init = init,
    .uninit = uninit,
    .query_formats = query_formats,
    .activate = activate,
    .inputs = gltransition_inputs,
    .outputs = gltransition_outputs,
    .priv_class = &gltransition_class,
    .flags = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC
};
