// ======================================================================== //
// Copyright 2009-2013 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#include "sys/platform.h"
#include "sys/stl/string.h"
#include "glutdisplay.h"
#include "regression.h"

/* include GLUT for display */
#if defined(__MACOSX__)
#  include <OpenGL/gl.h>
#  include <GLUT/glut.h>
#  include <ApplicationServices/ApplicationServices.h>
#elif defined(__WIN32__)
#  include <windows.h>
#  include <GL/gl.h>   
#  include <GL/glut.h>
#else
#  include <GL/gl.h>   
#  include <GL/glut.h>
#endif

#define USE_STEREO_MARGIN (0)

namespace embree
{
  /* logging settings */
  bool log_display = 1;

  /* camera settings */
  extern Vector3f g_camPos;
  extern Vector3f g_camLookAt;
  extern Vector3f g_camUp;
  extern float g_camFieldOfView;
  extern float g_camRadius;
  Handle<Device::RTCamera> createCamera(const AffineSpace3f& space, int stereoType = 0, float stereoDistance = 0.0f, int stereoPixelMargin = 0, float aspectRatio = 1.0);
  void clearGlobalObjects();

  // @syoyo
  static float gApplyDistortion = 1.0;
  extern std::string g_sampleMapFile;

  extern bool g_hmd;

  /* orbit camera model */
  static AffineSpace3f g_camSpace;
  static float theta;
  static float phi;
  static float psi;

  static bool g_showL = true;
  static bool g_showR = true;
  
  /* output settings */
  extern int g_refine;  
  extern bool g_fullscreen;
  extern bool g_hdrDisplay;
  extern size_t g_width, g_height;
  extern std::string g_format;
  extern int g_numBuffers;

  /* rendering device and global handles */
  extern Device *g_device;
  extern Handle<Device::RTRenderer> g_renderer;
  extern Handle<Device::RTToneMapper> g_tonemapper;
  extern Handle<Device::RTFrameBuffer> g_frameBuffer0;
  extern Handle<Device::RTFrameBuffer> g_frameBuffer1;
  extern Handle<Device::RTScene> g_render_scene;

  void setLight(Handle<Device::RTPrimitive> light);
  
  /* other stuff */
  bool g_resetAccumulation = false;

  /* pause mode */
  bool g_pause = false;

  /* regression testing */
  extern bool g_regression;

  /* ID of created window */
  static int g_window = 0;

  float angleX = 0, angleY = 0; // controllable light

  static float g_rotMat[16];
  static float g_prevRot[4] = {100, 100, 100, 100};
  static bool  g_hmdInitialized = false;
  static float g_hmdMove = 0.0;

  static float g_stereoOffset = 0.0; 
  static float g_rightOffset = 0.0; 

  //static int   g_stereoPixelMargin = 108; // @fixme 
#if USE_STEREO_MARGIN
  static int   g_stereoPixelMargin = 92; // @fixme 
#else
  static int   g_stereoPixelMargin = 0; // @fixme 
#endif

  const char*  g_fragShader = "../assets/postprocess.fs";
  const char*  g_vertShader = "../assets/postprocess.vs";

  typedef struct
  {
    GLuint  renderToTexID[2]; // stereo
    int     width, height;
    int     stereoMargin;

    // Postprocess pass
    GLuint  program;
    GLuint  vs; // vertex shader
    GLuint  fs; // fragment shader

    const char* vsSourceFile;
    const char* fsSourceFile;

    //
    float distortionXCenterOffset;
    float distortionScale;
    float distortionK[4];
    float distortionCA[4];  // chromatic aberration

  } RenderConfig;

  RenderConfig g_renderConfig;


  /*************************************************************************************************/
  /*                                  Keyboard control                                             */
  /*************************************************************************************************/

  static float g_speed = 1.0f;

  // Oculus parameters
  const float kWarpParam[4] = { 1.0, 0.22, 0.24, 0.0 };
  const float kChromaAbParam[4] = { 0.996000, -0.004000, 1.014000, 0.0 };
  const float kDistortionScale = 1.714606f;
  const float kDistortionXCenterOffset = 0.151976f;

  static inline void
  MatVMul(
    Vector3f&        w,
    float            m[16],
    const Vector3f&  v)
  {
    w.x = m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12];
    w.y = m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13];
    w.z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];
  };
        

  static inline void
  QuatToMatrix(
    float m[16],
    const float q[4])
  {
    double x2 = q[0] * q[0] * 2.0f;
    double y2 = q[1] * q[1] * 2.0f;
    double z2 = q[2] * q[2] * 2.0f;
    double xy = q[0] * q[1] * 2.0f;
    double yz = q[1] * q[2] * 2.0f;
    double zx = q[2] * q[0] * 2.0f;
    double xw = q[0] * q[3] * 2.0f;
    double yw = q[1] * q[3] * 2.0f;
    double zw = q[2] * q[3] * 2.0f;

    m[0] = (float) (1.0f - y2 - z2);
    m[1] = (float) (xy + zw);
    m[2] = -(float) (zx - yw);
    m[3] = 0.0f;

    m[4] = (float) (xy - zw);
    m[5] = (float) (1.0f - z2 - x2);
    m[6] = -(float) (yz + xw);
    m[7] = 0.0f;

    m[8] = (float) (zx + yw);
    m[9] = (float) (yz - xw);
    m[10] = -(float) (1.0f - x2 - y2);
    m[11] = 0.0f;

    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;

  }

#if 0
  bool
  GetMessageFromRedis(
    std::string& value,
    redisContext* ctx,
    const std::string& key)
  {
    std::string cmd;

    cmd += "GET ";
    cmd += key;

    redisReply* reply = reinterpret_cast<redisReply*>(redisCommand(ctx, cmd.c_str(), value.c_str()));
    if (reply->type == REDIS_REPLY_ERROR) {
      printf("err: %s\n", reply->str);
      return false;
    }

    if (reply->type == REDIS_REPLY_STRING) {
      value = std::string(reply->str);
    }

    freeReplyObject(reply);

    return true;
  }
#endif

  static void CheckGLErrors(std::string desc) {
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
      fprintf(stderr, "OpenGL error in \"%s\": %d (%d)\n", desc.c_str(), e, e);
      exit(20);
    }
  }

  static void
  SetupTex(
    GLuint texID,
    unsigned char* rgb,
    int w, int h)
  {
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef __linux__
    GLuint fmt = GL_BGRA;
#else
    GLuint fmt = GL_RGBA;
#endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, fmt, GL_UNSIGNED_BYTE, rgb);
    CheckGLErrors("glTexImage2D");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
  }

  static void
  UpdateTex(
    GLuint texID,
    unsigned char* rgb,
    int w, int h)
  {
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texID);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifdef __linux__
    GLuint fmt = GL_RGBA;
#else
    GLuint fmt = GL_RGBA;
#endif
    printf("w, h = %d, %d\n", w, h);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, rgb);
    CheckGLErrors("glTexSubImage2D");
    
  }


  static GLuint
  GenDummyTex(
    int width, int height)
  {
    GLuint texID;
    glGenTextures(1, &texID);

    std::vector<unsigned char> texImage;

    texImage.resize(width*height*4);

    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        unsigned char b = 0;
        if (x < width / 4) {
          texImage[4*(y*width+x)+0] = 255;
          texImage[4*(y*width+x)+1] = 0;
          texImage[4*(y*width+x)+2] = 0;
        } else {
          texImage[4*(y*width+x)+0] = 0;
          texImage[4*(y*width+x)+1] = 255;
          texImage[4*(y*width+x)+2] = 0;
        }
      }
    }
    
    SetupTex(texID, &texImage.at(0), width, height);

    return texID;
  }

  static bool
  CompileShader(
    GLenum shaderType,  // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER 
    GLuint& shader,
    const char* src)
  {
    GLint val = 0;

    // free old shader/program
    if (shader != 0) glDeleteShader(shader);

    shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &val);
    if (val != GL_TRUE) {
      char log[4096];
      GLsizei msglen;
      glGetShaderInfoLog(shader, 4096, &msglen, log);
      printf("%s\n", log);
      assert(val == GL_TRUE && "failed to compile shader");
    }

    printf("Compile shader OK\n");
    return true;
  }

  static bool
  LoadShader(
    GLenum shaderType,  // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER 
    GLuint& shader,
    const char* shaderSourceFilename)
  {
    GLint val = 0;

    // free old shader/program
    if (shader != 0) glDeleteShader(shader);

    printf("loading shader [ %s ]\n", shaderSourceFilename);

    static GLchar srcbuf[16384];
    FILE *fp = fopen(shaderSourceFilename, "rb");
    if (!fp) {
      fprintf(stderr, "failed to load shader: %s\n", shaderSourceFilename);
      return false;
    }
    fseek(fp, 0, SEEK_END);
    size_t len = ftell(fp);
    rewind(fp);
    len = fread(srcbuf, 1, len, fp);
    srcbuf[len] = 0;
    fclose(fp);

    static const GLchar *src = srcbuf;

    shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &val);
    if (val != GL_TRUE) {
      char log[4096];
      GLsizei msglen;
      glGetShaderInfoLog(shader, 4096, &msglen, log);
      printf("%s\n", log);
      assert(val == GL_TRUE && "failed to compile shader");
    }

    printf("Load shader [ %s ] OK\n", shaderSourceFilename);
    return true;
  }

  static bool
  LinkShader(
    GLuint& prog,
    GLuint& vertShader,
    GLuint& fragShader)
  {
    GLint val = 0;
    
    if (prog != 0) {
      glDeleteProgram(prog);
    }

    prog = glCreateProgram();

    glAttachShader(prog, vertShader);
    glAttachShader(prog, fragShader);
    glLinkProgram(prog);

    glGetProgramiv(prog, GL_LINK_STATUS, &val);
    assert(val == GL_TRUE && "failed to link shader");

    printf("Link shader OK\n");

    return true;
  }

  static bool
  PreparePostProcessShader(
    RenderConfig& config)
  {

    bool ret;

    config.vs = 0;
    ret = LoadShader(GL_VERTEX_SHADER, config.vs, config.vsSourceFile);
    assert(ret);

    config.fs = 0;
    ret = LoadShader(GL_FRAGMENT_SHADER, config.fs, config.fsSourceFile);
    assert(ret);

    ret = LinkShader(config.program, config.vs, config.fs);
    assert(ret);

    return true;
  }


  static bool
  GenRenderToTexture(
    RenderConfig& config)
  {
    glGenTextures(2, config.renderToTexID);
    CheckGLErrors("glGenTextures");

    for (int i = 0; i < 2; i++) {
      glBindTexture(GL_TEXTURE_2D, config.renderToTexID[i]);
      CheckGLErrors("glBindTextures");

      // Give an empty image to OpenGL
      glTexImage2D(GL_TEXTURE_2D, 0,GL_RGBA8, config.width + config.stereoMargin, config.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
      CheckGLErrors("glTexImage2D");

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    }

    return true;
  }

  static void
  ResizeTexture(RenderConfig& config) {

    glDeleteTextures(2, config.renderToTexID);
    CheckGLErrors("glDeleteTextures");
    //glDeleteFramebuffersEXT(1, &config.fboID);
    //CheckGLErrors("glDeleteFramebuffersEXT");

    GenRenderToTexture(config);

    //config.renderToTexID = GenDummyTex(config.width + config.stereoMargin, config.height); 
  }

  static bool
  PostProcessRender(
    const RenderConfig& config,
    int   eyeType)
  {
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    glUseProgram(config.program);

    float w = 0.5f; // vp.w / windowW
    float h = 1.0f; // vp.h / windowH
    float x = 0.0f; // vp.x / windowW
    float y = 0.0f; // vp.h / windowH

    float xoffsetScale = 1.0f;
    int xoffset = 0;
    if (eyeType == 2) { // right
      x = 0.5f;
      xoffset = config.width / 2.0f;
      xoffsetScale = -1.0f; // negate
    }
    
    float as = (config.width) / (float)config.height;
    printf("as = %f\n", as);

    float LensCenter[2];
    LensCenter[0] = x + (w + xoffsetScale * config.distortionXCenterOffset * 0.5f)*0.5f;
    LensCenter[1] = y + h*0.5f;
    printf("lensCenter = %f, %f\n", LensCenter[0], LensCenter[1]);

    float ScreenCenter[2];
    ScreenCenter[0] = x + w*0.5f;
    ScreenCenter[1] = y + h*0.5f;

    float scaleFactor = 1.0f / config.distortionScale;
    printf("scaleFactor = %f\n", scaleFactor);

    float Scale[2];
    Scale[0] = (w/2) * scaleFactor;
    Scale[1] = (h/2) * scaleFactor * as;

    float ScaleIn[2];
    ScaleIn[0] = (2/w);
    ScaleIn[1] = (2/h) / as;
    printf("scaleIn = %f, %f\n", ScaleIn[0], ScaleIn[1]);

    glUniform1i(glGetUniformLocation(config.program, "Texture0"), 0);
    CheckGLErrors("Texture0");

    glUniform1i(glGetUniformLocation(config.program, "Texture1"), 1);
    CheckGLErrors("Texture1");

    glUniform1f(glGetUniformLocation(config.program, "RightOffset"), g_rightOffset);
    CheckGLErrors("RightOffset");

    glUniform1f(glGetUniformLocation(config.program, "ApplyDistortion"), gApplyDistortion);
    CheckGLErrors("ApplyDistortion");

    glUniform2fv(glGetUniformLocation(config.program, "LensCenter"), 1, LensCenter);
    CheckGLErrors("LensCenter");
    glUniform2fv(glGetUniformLocation(config.program, "ScreenCenter"), 1, ScreenCenter);
    CheckGLErrors("ScreenCenter");

    glUniform1f(glGetUniformLocation(config.program, "XCenterOffset"), config.distortionXCenterOffset);
    CheckGLErrors("XCenterOffset");

    int halfW = config.width;
    glUniform1f(glGetUniformLocation(config.program, "XWidth"), halfW / (float)(halfW + config.stereoMargin));
    CheckGLErrors("XWidth");

    //printf("XWidth = %f (w:%d, m:%d)\n", halfW / (float)(halfW + config.stereoMargin), halfW, config.stereoMargin);

    glUniform1f(glGetUniformLocation(config.program, "XMargin"), config.stereoMargin / (float)(halfW + config.stereoMargin));
    CheckGLErrors("XMargin");

    //printf("XMargin = %f (w:%d, m:%d)\n", config.stereoMargin / (float)(halfW + config.stereoMargin), halfW, config.stereoMargin);

    glUniform2fv(glGetUniformLocation(config.program, "Scale"), 1, Scale);
    CheckGLErrors("Scale");
    glUniform2fv(glGetUniformLocation(config.program, "ScaleIn"), 1, ScaleIn);
    CheckGLErrors("ScaleIn");
    glUniform4fv(glGetUniformLocation(config.program, "HmdWarpParam"), 1, config.distortionK);
    CheckGLErrors("HmdWarpParam");
    glUniform4fv(glGetUniformLocation(config.program, "ChromAbParam"), 1, config.distortionCA);
    CheckGLErrors("ChromAbParam");

    //GLuint texID = glGetUniformLocation(config.program, "renderedTexture");
    float eye_offset_x = 0.0f;
    if (eyeType == 2) { // right
      eye_offset_x = 0.5f;
    }
    glUniform1f(glGetUniformLocation(config.program, "eye_offset_x"), eye_offset_x);
    CheckGLErrors("eye_offset_x");

    //printf("g_width = %d\n", g_width);
    glViewport(0, 0, g_width, g_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, config.renderToTexID[0]);
    CheckGLErrors("glBindTexture");
    glEnable(GL_TEXTURE_2D);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, config.renderToTexID[1]);
    CheckGLErrors("glBindTexture");
    glEnable(GL_TEXTURE_2D);

    glActiveTexture(GL_TEXTURE0);

    //glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glColor3f(0.0, 0.0, 1.0);
    glBegin(GL_QUADS);
        // texcoord upside down
        glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f, 1.0f, -0.5f); // Upper Left
        glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f, 1.0f, -0.5f); // Upper Right
        glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f,-1.0f, -0.5f); // Lower Right
        glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f,-1.0f, -0.5f); // Lower Left
    glEnd();

    glFlush();
    glEnable(GL_DEPTH_TEST);

    return true;
  }

  void InitRenderConfig(RenderConfig& config, int width, int height, int stereoMargin) {
    config.distortionK[0] = kWarpParam[0];
    config.distortionK[1] = kWarpParam[1];
    config.distortionK[2] = kWarpParam[2];
    config.distortionK[3] = kWarpParam[3];

    config.distortionCA[0] = kChromaAbParam[0];
    config.distortionCA[1] = kChromaAbParam[1];
    config.distortionCA[2] = kChromaAbParam[2];
    config.distortionCA[3] = kChromaAbParam[3];

    config.distortionScale = kDistortionScale;
    config.distortionXCenterOffset = kDistortionXCenterOffset;

    config.width  = width;
    config.height = height;
    config.stereoMargin =stereoMargin;

    config.vs = 0;
    config.fs = 0;

    config.vsSourceFile = g_vertShader;
    config.fsSourceFile = g_fragShader;
  }

  int inline fasterfloor( const float x ) {
    if (x >= 0) {
      return (int)x;
    }

    int y = (int)x;
    if (std::abs(x - y) <= std::numeric_limits<float>::epsilon()) {
      // Do nothing.
    } else {
      y = y - 1;
    }

    return y;
  }

  inline void FilterFloat(
    float* rgba,
    const float* image,
    int i00, int i10, int i01, int i11,
    float w[4], // weight
    int stride)
  {
    float texel[4][4];

    if (stride == 4) {

      for (int i = 0; i < 4; i++) {
        texel[0][i] = image[i00+i];
        texel[1][i] = image[i10+i];
        texel[2][i] = image[i01+i];
        texel[3][i] = image[i11+i];
      }

      for (int i = 0; i < 4; i++) {
        rgba[i] = w[0] * texel[0][i] +
                  w[1] * texel[1][i] +
                  w[2] * texel[2][i] +
                  w[3] * texel[3][i];
      }

    } else {

      for (int i = 0; i < stride; i++) {
          texel[0][i] = image[i00+i];
          texel[1][i] = image[i10+i];
          texel[2][i] = image[i01+i];
          texel[3][i] = image[i11+i];
      }

      for (int i = 0; i < stride; i++) {
        rgba[i] = w[0] * texel[0][i] +
                  w[1] * texel[1][i] +
                  w[2] * texel[2][i] +
                  w[3] * texel[3][i];
      }
    }

    if (stride < 4) {
      rgba[3] = 0.0;
    }

  }

  inline void FilterByte(
    float* rgba,
    const unsigned char* image,
    int i00, int i10, int i01, int i11,
    float w[4], // weight
    int stride)
  {
    unsigned char texel[4][4];

    const float inv = 1.0f / 255.0f;
    if (stride == 4) {

      for (int i = 0; i < 4; i++) {
        texel[0][i] = image[i00+i];
        texel[1][i] = image[i10+i];
        texel[2][i] = image[i01+i];
        texel[3][i] = image[i11+i];
        //printf("texel = %d, %d, %d, %d\n",
        //  texel[0][i],
        //  texel[1][i],
        //  texel[2][i],
        //  texel[3][i]);
      }

      for (int i = 0; i < 4; i++) {
        rgba[i] = w[0] * texel[0][i] +
                  w[1] * texel[1][i] +
                  w[2] * texel[2][i] +
                  w[3] * texel[3][i];
        // normalize.
        rgba[i] *= inv;
      }

    } else {

      for (int i = 0; i < stride; i++) {
          texel[0][i] = image[i00+i];
          texel[1][i] = image[i10+i];
          texel[2][i] = image[i01+i];
          texel[3][i] = image[i11+i];
      }

      for (int i = 0; i < stride; i++) {
        rgba[i] = w[0] * texel[0][i] +
                  w[1] * texel[1][i] +
                  w[2] * texel[2][i] +
                  w[3] * texel[3][i];
        // normalize.
        rgba[i] *= inv;
      }
    }

    if (stride < 4) {
      rgba[3] = 0.0;
    }

  }

  /* structure

      <--------------- x_stride ------------------->

      +------+------------------------------+------+  \
      |      |                              |      |  |
      |      |                              |      |  |
      |      |                              |      |  
      |      |                              |      |  height
      |      |                              |      | 
      |      |                              |      |  |
      |      |                              |      |  |
      |      |                              |      |  |
      |      |                              |      |  |
      +------+------------------------------+------+  /

      <-----> <-------- width -------------> 
     x_offset


      x_offset + width must be smaller than x_stride.
  */
  void
  fetch_texture(
      float *rgba,
      float u, float v,
      const unsigned char* image /* rgba */, int width, int height, int x_stride, int x_offset, int components)
  {
      float sx = fasterfloor(u);
      float sy = fasterfloor(v);

      float uu = u - sx;
      float vv = v - sy;

      // clamp
      uu = std::max(uu, 0.0f); uu = std::min(uu, 1.0f);
      vv = std::max(vv, 0.0f); vv = std::min(vv, 1.0f);

      float px = (width  - 1) * uu;
      float py = (height - 1) * vv;

      int x0 = (int)px;
      int y0 = (int)py;
      int x1 = ((x0 + 1) >= width ) ? (width  - 1) : (x0 + 1);
      int y1 = ((y0 + 1) >= height) ? (height - 1) : (y0 + 1);

      float dx = px - (float)x0;
      float dy = py - (float)y0;

      float w[4];

      w[0] = (1.0f - dx) * (1.0 - dy);
      w[1] = (1.0f - dx) * (      dy);
      w[2] = (       dx) * (1.0 - dy);
      w[3] = (       dx) * (      dy);

      
      //unsigned char texel[4][4];

      int c = components;

      // add offset
      x0 = std::min(x0 + x_offset, (x_stride-1));
      x1 = std::min(x1 + x_offset, (x_stride-1));

      int i00 = c * (y0 * x_stride + x0);
      int i01 = c * (y0 * x_stride + x1);
      int i10 = c * (y1 * x_stride + x0);
      int i11 = c * (y1 * x_stride + x1);

      FilterByte(rgba, image, i00, i10, i01, i11, w, c);
  }


  // @syoyo ; Oclulus distortion and fetch
  bool BarrelDistortionFilter(float color[4], const unsigned char* image, int width, int height, int x_stride, int x_offset, float u, float v, const float scale[2], const float scaleIn[2], const float warpParam[4], const float screenCenter[2], const float lensCenter[2], const float chromaAbParam[4]) {

    // scale s to [-1, 1]
    float tx = (0.5f*u - lensCenter[0]) * scaleIn[0]; // half W
    float ty = (v - lensCenter[1]) * scaleIn[1];

    //printf("uv = %f, %f\n", u, v);
    //printf("tx, ty = %f, %f\n", tx, ty);

    float rSq = tx * tx + ty * ty;

    float theta1x = tx * (warpParam[0] + warpParam[1] * rSq + warpParam[2] * rSq * rSq + warpParam[3] * rSq * rSq * rSq);
    float theta1y = ty * (warpParam[0] + warpParam[1] * rSq + warpParam[2] * rSq * rSq + warpParam[3] * rSq * rSq * rSq);

    float thetaBluex = theta1x * (chromaAbParam[2] + chromaAbParam[3] * rSq);
    float thetaBluey = theta1y * (chromaAbParam[2] + chromaAbParam[3] * rSq);
    float tcBluex = lensCenter[0] + scale[0] * thetaBluex;
    float tcBluey = lensCenter[1] + scale[1] * thetaBluey;

    //printf("tcBlue = %f, %f\n", tcBluex, tcBluey);

    // out of sight?
    if ((tcBluex < screenCenter[0] - 0.25) || (tcBluex > screenCenter[0] + 0.25) || ((tcBluey < screenCenter[1] - 0.5)) || (tcBluey > screenCenter[1] + 0.5)) {
      return false;
    }

    // @todo { chromatic abbrebiation }
    float uu = lensCenter[0] + scale[0] * theta1x;
    float vv = lensCenter[1] + scale[1] * theta1y;

    int components = 4;

    //printf("uv = %f, %f\n", uu, vv);
    fetch_texture(color, 2.0f*uu, vv, image, width, height, x_stride, x_offset, components);
    //printf("c = %f\n", color[0]);
    //color[0] = uu;
    //color[1] = vv;
    //color[2] = 1.0;
    //color[3] = 1.0;

    return true;
  }

  bool SimpleFilter(float color[4], const unsigned char* image, int width, int height, int x_stride, int x_offset, float u, float v) {

    //printf("uv = %f, %f, w = %d, h = %d\n", u, v, width, height);
    fetch_texture(color, u, v, image, width, height, x_stride, x_offset, 4);
    //printf("c = %f, %f, %f, %f\n", color[0], color[1], color[2], color[3]);

    return true;
  }

  inline unsigned char clamp(float x) {
    int i = (int)x;
    return (unsigned char)(std::min(std::max(i, 0), 255));
  }

  void Postfilter(unsigned char* rgba, const unsigned char* image /* rgba*/, int w, int h, int x_stride, float x_offset, int stereoTy) {

    // left setting.
    float xoffset = 0.0f;
    float xoffsetScale = 1.0f;
    float xstereoDist = 0; //-g_stereoDistance/2.0f/w;

    if (stereoTy == 1) { // right setting
      xoffset = 0.0f;
      xoffsetScale = -1.0;
      xstereoDist = 0; //g_stereoDistance/2.0f/w;
    }

    float viewW = 0.5f; // vp.w / windowW
    float viewH = 1.0f; // vp.h / windowH
    float aspectRatio = w / (float)h;
    //printf("w= %d, h = %d, aspect ratio = %f\n", swapchain->getWidth(), swapchain->getHeight(), aspectRatio);

    float scaleIn[2];
    scaleIn[0] = (2/viewW);
    scaleIn[1] = (2/viewH) / aspectRatio;
    //printf("scaleIn = %f, %f\n", scaleIn[0], scaleIn[1]);

    // left.

    float screenCenter[2];
    screenCenter[0] = xoffset + viewW * 0.5f;
    screenCenter[1] = viewH * 0.5f;

    float scaleFactor = 1.0f / kDistortionScale;

    float scale[2];
    scale[0] = (viewW/2.0f) * scaleFactor;
    scale[1] = (viewH/2.0f) * scaleFactor * aspectRatio;


    float lensCenter[2];
    lensCenter[0] = xoffset + (viewW + xoffsetScale * kDistortionXCenterOffset * 0.5f)*0.5f;
    lensCenter[1] = viewH*0.5f;


    for (int y = 0; y < h; y++) {
      float v = (y+0.5f) / (float)h;

      for (int x = 0; x < w; x++) {
        float u = (x+0.5f) / (float)w;

        float col[4] = {0.0f, 0.0f, 0.0f, 0.0f};
#if 1
        bool ret = BarrelDistortionFilter(col, image, w, h, x_stride, x_offset, u, v, scale, scaleIn, kWarpParam, screenCenter, lensCenter, kChromaAbParam);
        //bool ret = SimpleFilter(col, image, w, h, u, v);
        //printf("col = %f, %f, %f, %f\n", col[0], col[1], col[2], col[3]);

        rgba[4*(y*w+x)+0] = clamp(col[0] * 255.5f);
        rgba[4*(y*w+x)+1] = clamp(col[1] * 255.5f);
        rgba[4*(y*w+x)+2] = clamp(col[2] * 255.5f);
        rgba[4*(y*w+x)+3] = clamp(col[3] * 255.5f);
#else
        rgba[4*(y*w+x)+0] = image[4*(y*w+x)+0];
        rgba[4*(y*w+x)+1] = image[4*(y*w+x)+1];
        rgba[4*(y*w+x)+2] = image[4*(y*w+x)+2];
        rgba[4*(y*w+x)+3] = image[4*(y*w+x)+3];
#endif
      }
    }
  }

  void keyboardFunc(unsigned char k, int, int)
  {
    switch (k)
    {
    case ' ' : {
      g_pause = !g_pause;
      break;
    }
    case 'd' :
      if (gApplyDistortion > 0.5) {
        gApplyDistortion = 0.0;
      } else {
        gApplyDistortion = 1.0;
      }
      break; 
    case 'c' : {
      AffineSpace3f cam(g_camSpace.l,g_camSpace.p);
      std::cout << "-vp " << g_camPos.x    << " " << g_camPos.y    << " " << g_camPos.z    << " " << std::endl
                << "-vi " << g_camLookAt.x << " " << g_camLookAt.y << " " << g_camLookAt.z << " " << std::endl
                << "-vu " << g_camUp.x     << " " << g_camUp.y     << " " << g_camUp.z     << " " << std::endl;
      break;
    }
    case 'f' : glutFullScreen(); break;
    case 'r' : g_refine = !g_refine; break;
    case 't' : g_regression = !g_regression; break;
    case 'n' : g_showL = !g_showL; break;
    case 'm' : g_showR = !g_showR; break;
    case 'l' : g_camRadius = max(0.0f, g_camRadius-1); break;
    case 'L' : g_camRadius += 1; break;

    case 'u' : 
      g_rightOffset -= 0.005;
      g_rightOffset = std::max(g_rightOffset, 0.0f);
      printf("right offset = %f\n", g_rightOffset);
      break;
    case 'i' : 
      g_rightOffset += 0.005;
      printf("right offset = %f\n", g_rightOffset);
      break;

    case 'j' : {
      g_stereoOffset += 0.1;
      printf("stereo offset = %f\n", g_stereoOffset);
      //g_stereoPixelMargin += 1;
      //printf("stereo margin = %d\n", g_stereoPixelMargin);
      //g_renderConfig.stereoMargin = g_stereoPixelMargin;
      //ResizeTexture(g_renderConfig);
      //g_frameBuffer0 = g_device->rtNewFrameBuffer(g_format.c_str(),g_width/2+g_stereoPixelMargin,g_height,g_numBuffers);
      break;
    }
    case 'k' : {
      g_stereoOffset -= 0.1;
      //g_stereoOffset = std::max(g_stereoOffset, 0.0f);
      printf("stereo offset = %f\n", g_stereoOffset);
      //g_stereoPixelMargin -= 1;
      //g_stereoPixelMargin = std::max(g_stereoPixelMargin, 0);
      //printf("stereo = margin %d\n", g_stereoPixelMargin); 
      //g_renderConfig.stereoMargin = g_stereoPixelMargin;
      //ResizeTexture(g_renderConfig);
      //g_frameBuffer0 = g_device->rtNewFrameBuffer(g_format.c_str(),g_width/2+g_stereoPixelMargin,g_height,g_numBuffers);
      break;
      }
    case '\033': case 'q': case 'Q':
      clearGlobalObjects();
      glutDestroyWindow(g_window);
      exit(0);
      break;
    }

    g_resetAccumulation = true;
  }

  void specialFunc(int k, int, int)
  {
    if (glutGetModifiers() == GLUT_ACTIVE_CTRL)
    {
      switch (k) {
      case GLUT_KEY_LEFT      : angleX+=0.1f; break;
      case GLUT_KEY_RIGHT     : angleX-=0.1f; break;
      case GLUT_KEY_UP        : angleY+=0.1f; if (angleY > 0.5f*float(pi)) angleY = 0.5f*float(pi); break;
      case GLUT_KEY_DOWN      : angleY-=0.1f; if (angleY < 0) angleY = 0.0f; break;
      }
    } else {
      switch (k) {
      case GLUT_KEY_LEFT      : g_camSpace = AffineSpace3f::rotate(g_camSpace.p,g_camUp,-0.01f) * g_camSpace; break;
      case GLUT_KEY_RIGHT     : g_camSpace = AffineSpace3f::rotate(g_camSpace.p,g_camUp,+0.01f) * g_camSpace; break;
      case GLUT_KEY_UP        : g_camSpace = g_camSpace * AffineSpace3f::translate(Vector3f(0,0,g_speed)); break;
      case GLUT_KEY_DOWN      : g_camSpace = g_camSpace * AffineSpace3f::translate(Vector3f(0,0,-g_speed)); break;
      case GLUT_KEY_PAGE_UP   : g_speed *= 1.2f; std::cout << "speed = " << g_speed << std::endl; break;
      case GLUT_KEY_PAGE_DOWN : g_speed /= 1.2f; std::cout << "speed = " << g_speed << std::endl; break;
      }
    }
    g_resetAccumulation = true;
  }

  /*************************************************************************************************/
  /*                                   Mouse control                                               */
  /*************************************************************************************************/

  static int mouseMode = 0;
  static int clickX = 0, clickY = 0;

  void clickFunc(int button, int state, int x, int y)
  {
    if (state == GLUT_UP) {
      mouseMode = 0;
      if (button == GLUT_LEFT_BUTTON && glutGetModifiers() == GLUT_ACTIVE_CTRL) {
        Handle<Device::RTCamera> camera = createCamera(AffineSpace3f(g_camSpace.l,g_camSpace.p));
        Vector3f p;
        bool hit = g_device->rtPick(camera, x / float(g_width), y / float(g_height), g_render_scene, p.x, p.y, p.z);
        if (hit) {
          Vector3f delta = p - g_camLookAt;
          Vector3f right = cross(normalize(g_camUp),normalize(g_camLookAt-g_camPos));
          Vector3f offset = dot(delta,right)*right + dot(delta,g_camUp)*g_camUp;
          g_camLookAt = p;
          g_camPos += offset;
          g_camSpace = AffineSpace3f::lookAtPoint(g_camPos, g_camLookAt, g_camUp);
          g_resetAccumulation = true;
        }
      }
      else if (button == GLUT_LEFT_BUTTON && glutGetModifiers() == (GLUT_ACTIVE_CTRL | GLUT_ACTIVE_SHIFT)) {
        Handle<Device::RTCamera> camera = createCamera(AffineSpace3f(g_camSpace.l,g_camSpace.p));
        Vector3f p;
        bool hit = g_device->rtPick(camera, x / float(g_width), y / float(g_height), g_render_scene, p.x, p.y, p.z);
        if (hit) {
          Vector3f v = normalize(g_camLookAt - g_camPos);
          Vector3f d = p - g_camPos;
          g_camLookAt = g_camPos + v*dot(d,v);
          g_camSpace = AffineSpace3f::lookAtPoint(g_camPos, g_camLookAt, g_camUp);
          g_resetAccumulation = true;
        }
      }
    }
    else {
      if (glutGetModifiers() == GLUT_ACTIVE_CTRL) return;
      clickX = x; clickY = y;
      if      (button == GLUT_LEFT_BUTTON && glutGetModifiers() == GLUT_ACTIVE_ALT) mouseMode = 4;
      else if (button == GLUT_LEFT_BUTTON)   mouseMode = 1;
      else if (button == GLUT_MIDDLE_BUTTON) mouseMode = 2;
      else if (button == GLUT_RIGHT_BUTTON)  mouseMode = 3;
    }
  }

  void motionFunc(int x, int y)
  {
    float dClickX = float(clickX - x), dClickY = float(clickY - y);
    clickX = x; clickY = y;

    // Rotate camera around look-at point (LMB + mouse move)
    if (mouseMode == 1) {
#define ROTATE_WITH_FIXED_UPVECTOR 1
#if ROTATE_WITH_FIXED_UPVECTOR
      float angularSpeed = 0.5f / 180.0f * float(pi);
      float theta = dClickX * angularSpeed;
      float phi = dClickY * angularSpeed;

      const Vector3f viewVec = normalize(g_camLookAt - g_camPos);
      float dist = length(g_camLookAt - g_camPos);
      
      const Vector3f dX = normalize(cross(viewVec,g_camUp));
      const Vector3f dY = normalize(cross(viewVec,dX));

      AffineSpace3f rot_x = AffineSpace3f::rotate(g_camLookAt,dX,phi);

      g_camSpace = rot_x * g_camSpace; 
      g_camSpace = AffineSpace3f::rotate(g_camLookAt,dY,theta) * g_camSpace; 
      g_camPos = g_camLookAt-dist*xfmVector(g_camSpace,Vector3f(0,0,1));
#else
      float angularSpeed = 0.05f / 180.0f * float(pi);
      float mapping = 1.0f;
      if (g_camUp[1] < 0) mapping = -1.0f;
      theta -= mapping * dClickX * angularSpeed;
      phi += dClickY * angularSpeed;

      if (theta < 0) theta += 2.0f * float(pi);
      if (theta > 2.0f*float(pi)) theta -= 2.0f * float(pi);
      if (phi < -1.5f*float(pi)) phi += 2.0f*float(pi);
      if (phi > 1.5f*float(pi)) phi -= 2.0f*float(pi);

      float cosPhi = cosf(phi);
      float sinPhi = sinf(phi);
      float cosTheta = cosf(theta);
      float sinTheta = sinf(theta);
      float dist = length(g_camLookAt - g_camPos);
      g_camPos = g_camLookAt + dist * Vector3f(cosPhi * sinTheta, -sinPhi, cosPhi * cosTheta);
      Vector3f viewVec = normalize(g_camLookAt - g_camPos);
      Vector3f approxUp(0.0f, 1.0f, 0.0f);
      if (phi < -0.5f*float(pi) || phi > 0.5*float(pi)) approxUp = -approxUp;
      Vector3f rightVec = normalize(cross(viewVec, approxUp));
      AffineSpace3f rotate = AffineSpace3f::rotate(viewVec, psi);
      g_camUp = xfmVector(rotate, cross(rightVec, viewVec));
#endif
    }
    // Pan camera (MMB + mouse move)
    if (mouseMode == 2) {
      float panSpeed = 0.00025f;
      float dist = length(g_camLookAt - g_camPos);
      Vector3f viewVec = normalize(g_camLookAt - g_camPos);
      Vector3f strafeVec = cross(g_camUp, viewVec);
      Vector3f deltaVec = strafeVec * panSpeed * dist * float(dClickX)
        + g_camUp * panSpeed * dist * float(-dClickY);
      g_camPos += deltaVec;
      g_camLookAt += deltaVec;
    }
    // Dolly camera (RMB + mouse move)
    if (mouseMode == 3) {
      float dollySpeed = 0.01f;
      float delta;
      if (fabsf(dClickX) > fabsf(dClickY)) delta = float(dClickX);
      else delta = float(-dClickY);
      float k = powf((1-dollySpeed), delta);
      float dist = 100.0f; //length(g_camLookAt - g_camPos);
      Vector3f viewVec = normalize(g_camLookAt - g_camPos);
      g_camPos += dist * (1-k) * viewVec;
      //g_camLookAt += dist * (1-k) * viewVec;
    }
    // Roll camera (ALT + LMB + mouse move)
    if (mouseMode == 4) {
      float angularSpeed = 0.1f / 180.0f * float(pi);
      psi -= dClickX * angularSpeed;
      Vector3f viewVec = normalize(g_camLookAt - g_camPos);
      Vector3f approxUp(0.0f, 1.0f, 0.0f);
      if (phi < -0.5f*float(pi) || phi > 0.5*float(pi)) approxUp = -approxUp;
      Vector3f rightVec = normalize(cross(viewVec, approxUp));
      AffineSpace3f rotate = AffineSpace3f::rotate(viewVec, psi);
      g_camUp = xfmVector(rotate, cross(rightVec, viewVec));
    }

    g_camSpace = AffineSpace3f::lookAtPoint(g_camPos, g_camLookAt, g_camUp);
    g_resetAccumulation = true;

  }

 
  /*************************************************************************************************/
  /*                                   Window control                                              */
  /*************************************************************************************************/

  const size_t avgFrames = 4;
  double g_t0 = getSeconds();
  double g_dt[avgFrames] = { 0.0f };
  size_t frameID = 0;
  
  void displayFunc(void)
  {
    if (g_pause)
      return;
    
    /* create random geometry for regression test */
    if (g_regression)
      g_render_scene = createRandomScene(g_device,1,random<int>()%100,random<int>()%1000);

    /* set accumulation mode */
    int accumulate = g_resetAccumulation ? 0 : g_refine;
    g_resetAccumulation = false;


    // @syoyo: SREREO
    float scale = 100.0;
    Vector3f camPosL = g_camPos + Vector3f(-scale*g_stereoOffset/2.0, 0.0, 0.0);
    Vector3f camLookAtL = g_camLookAt + Vector3f(-scale*g_stereoOffset/2.0, 0.0, 0.0);
    AffineSpace3f camSpaceL = AffineSpace3f::lookAtPoint(camPosL, camLookAtL, g_camUp);

    Vector3f camPosR = g_camPos + Vector3f(scale*g_stereoOffset/2.0, 0.0, 0.0);
    Vector3f camLookAtR = g_camLookAt + Vector3f(scale*g_stereoOffset/2.0, 0.0, 0.0);
    AffineSpace3f camSpaceR = AffineSpace3f::lookAtPoint(camPosR, camLookAtR, g_camUp);

    /* render image */
    Handle<Device::RTCamera> cameraL = createCamera(AffineSpace3f(camSpaceL.l, camSpaceL.p), 0, 0, g_stereoPixelMargin, 0.5f*g_width/(float)g_height);
    Handle<Device::RTCamera> cameraR = createCamera(AffineSpace3f(camSpaceR.l,camSpaceR.p), 1, 0, 0, 0.5f*g_width/(float)g_height);

    /* render into framebuffer */
    g_device->rtRenderFrame(g_renderer,cameraL,g_render_scene,g_tonemapper,g_frameBuffer0,accumulate);

    g_device->rtRenderFrame(g_renderer,cameraR,g_render_scene,g_tonemapper,g_frameBuffer1,accumulate);

    g_device->rtSwapBuffers(g_frameBuffer0);
    g_device->rtSwapBuffers(g_frameBuffer1);

    /* draw image in OpenGL */
    double render_t0 = getSeconds();

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

#if USE_STEREO_MARGIN

    void* src_ptr = g_device->rtMapFrameBuffer(g_frameBuffer0);
    UpdateTex(g_renderConfig.renderToTexID, (unsigned char*)src_ptr, g_width/2+g_stereoPixelMargin, g_height);

    // left
    if (g_showL) {

      assert(g_format == "RGBA8");

#if 0 // Software
      unsigned char* dst_ptr = new unsigned char[4*(g_width/2+g_stereoPixelMargin)*g_height]; 

      //unsigned char* uc_ptr = (unsigned char*)(src_ptr);
      //for (int i = 0; i < 4*(g_width/2+g_stereoPixelMargin)*g_height; i++) {
      //  uc_ptr[i] = 100;
      //}
  
      // output resolution is (g_width/2, g_height)
      Postfilter(dst_ptr, (unsigned char*)src_ptr, g_width/2, g_height, g_width/2+g_stereoPixelMargin, 0, /*stereoTy=*/0);
      void* ptr= dst_ptr;

      glRasterPos2i(-1, 1);
      glPixelZoom(1.0f, -1.0f);

      if (g_format == "RGB_FLOAT32")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGB,GL_FLOAT,ptr);
      else if (g_format == "RGBA8")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGBA,GL_UNSIGNED_BYTE,ptr);
      else if (g_format == "RGB8")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGB,GL_UNSIGNED_BYTE,ptr);
      else 
      throw std::runtime_error("unknown framebuffer format: "+g_format);
      delete [] dst_ptr;
#else
      PostProcessRender(g_renderConfig, /*eyeType=*/0);
#endif

    }

    // right
    if (0) {
      // Reuse buffer L + offset with stereo pixel margin.
      unsigned char* dst_ptr = new unsigned char[4*(g_width/2+g_stereoPixelMargin)*g_height]; 
      assert(g_format == "RGBA8");

      // output resolution is (g_width/2, g_height)
      Postfilter(dst_ptr, (unsigned char*)src_ptr, g_width/2, g_height, g_width/2+g_stereoPixelMargin, g_stereoPixelMargin, /*stereoTy=*/1);
      void* ptr= dst_ptr;

      glRasterPos2i(0, 1);
      glPixelZoom(1.0f, -1.0f);

      if (g_format == "RGB_FLOAT32")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGB,GL_FLOAT,ptr);
      else if (g_format == "RGBA8")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGBA,GL_UNSIGNED_BYTE,ptr);
      else if (g_format == "RGB8")
        glDrawPixels((GLsizei)g_width/2,(GLsizei)g_height,GL_RGB,GL_UNSIGNED_BYTE,ptr);
      else 
      throw std::runtime_error("unknown framebuffer format: "+g_format);

      delete [] dst_ptr;
    }
#else

    UpdateTex(g_renderConfig.renderToTexID[0], (unsigned char*)g_device->rtMapFrameBuffer(g_frameBuffer0), g_width/2+g_stereoPixelMargin, g_height);

    UpdateTex(g_renderConfig.renderToTexID[1], (unsigned char*)g_device->rtMapFrameBuffer(g_frameBuffer1), g_width/2+g_stereoPixelMargin, g_height);

    assert(g_format == "RGBA8");

    PostProcessRender(g_renderConfig, /*eyeType=*/0);

#endif
                                                    
    glFlush();
    glutSwapBuffers();

    g_device->rtUnmapFrameBuffer(g_frameBuffer0);
    g_device->rtUnmapFrameBuffer(g_frameBuffer1);

    double render_t1 = getSeconds();

    /* calculate rendering time */
    double t1 = getSeconds();
    g_dt[frameID % avgFrames] = t1-g_t0; g_t0 = t1;
    frameID++;

    /* print average render time of previous frames */
    size_t num = 0;
    double dt = 0.0f;
    for (size_t i=0; i<avgFrames; i++) {
      if (g_dt[i] != 0.0f) {
        dt += g_dt[i]; num++;
      }
    }
    dt /= num;

    std::ostringstream stream;
    stream.setf(std::ios::fixed, std::ios::floatfield);
    stream.precision(2);
    stream << 1.0f/dt << " fps, ";
    stream.precision(2);
    stream << dt*1000.0f << " ms";
    stream << ", " << g_width << "x" << g_height;
    if (log_display)
      std::cout << "display " << stream.str() << std::endl;
    glutSetWindowTitle((std::string("Embree: ") + stream.str()).c_str());

    std::cout << "glrender: " << render_t1 - render_t0 << " secs" << std::endl;

  }

  void reshapeFunc(int w, int h) {
    if (g_width == size_t(w) && g_height == size_t(h)) return;
    glViewport(0, 0, w, h);
    g_width = w; g_height = h;

    g_renderConfig.width = g_width/2;
    g_renderConfig.height = g_height;

    printf("reshape %d x %d\n", g_renderConfig.width, g_renderConfig.height);

    //ResizeTexture(g_renderConfig);

    //g_renderConfig.width = g_width;

    // @syoyo: 
    g_frameBuffer0 = g_device->rtNewFrameBuffer(g_format.c_str(),w/2+g_stereoPixelMargin,h,g_numBuffers);
    g_frameBuffer1 = g_device->rtNewFrameBuffer(g_format.c_str(),w/2,h,g_numBuffers);
    glViewport(0, 0, (GLsizei)g_width, (GLsizei)g_height);
    g_resetAccumulation = true;
  }

#if 0
  void ParseHMDRotation(const std::string& msg) {
    picojson::value v;
    char* s = const_cast<char*>(msg.c_str());
    std::string err = picojson::parse(v, s, s + msg.size());

    if (!err.empty()) {
      std::cerr << err << std::endl;
      return;
    }

    double qx = v.get("qx").get<double>();
    double qy = v.get("qy").get<double>();
    double qz = v.get("qz").get<double>();
    double qw = v.get("qw").get<double>();

    //printf("q = %f, %f, %f, %f\n", qx, qy, qz, qw);

    float quat[4];
    quat[0] = qx;
    quat[1] = qy;
    quat[2] = qz;
    quat[3] = qw;

    QuatToMatrix(g_rotMat, quat);

    if (!g_hmdInitialized) {
      g_hmdInitialized = true;
    }

    float diff[4];
    diff[0] = (qx - g_prevRot[0]);
    diff[1] = (qy - g_prevRot[1]);
    diff[2] = (qz - g_prevRot[2]);
    diff[3] = (qw - g_prevRot[3]);

    g_hmdMove = fabs(diff[0]) + fabs(diff[1]) + fabs(diff[2]) + fabs(diff[3]);
    printf("diff = %f\n", g_hmdMove);


    g_prevRot[0] = qx;
    g_prevRot[1] = qy;
    g_prevRot[2] = qz;
    g_prevRot[3] = qw;

  }
#endif

  void UpdateCamera() {
  
    if (g_hmdMove < 0.002) { // heuristics
      return;
    }

    Vector3f up(0, 1, 0);
    Vector3f look(0, 0, -1);
    Vector3f r;
    Vector3f upV;
    MatVMul(r, g_rotMat, look);
    MatVMul(upV, g_rotMat, up);

    printf("r = %f, %f, %f\n", r[0], r[1], r[2]);
    printf("upV = %f, %f, %f\n", upV[0], upV[1], upV[2]);

    g_camLookAt = g_camPos + r;

    g_camUp = upV;
    g_resetAccumulation = true;
  }

  void idleFunc() {

#if 0
    //
    // get HMD rotation from redis
    //
    if (g_redisCtx && g_hmd) {
      std::string msg;
      bool ret = GetMessageFromRedis(msg, g_redisCtx, "hmd_rotation");
      if (ret) {
        //printf("redis msg = %s\n", msg.c_str());
        ParseHMDRotation(msg);

        UpdateCamera();
      }
    }
#endif
 
    glutPostRedisplay();
  }

  void GLUTDisplay(const AffineSpace3f& camera, float s, Handle<Device::RTScene>& scene)
  {
    g_camSpace = camera;
    g_speed = s;
    g_render_scene = scene;
    scene = NULL; // GLUT will never end this function, thus cleanup scene by hand


    /* initialize orbit camera model */
    Vector3f viewVec = normalize(g_camLookAt - g_camPos);
    theta = atan2f(-viewVec.x, -viewVec.z);
    phi = asinf(viewVec.y);
    Vector3f approxUp(0.0f, 1.0f, 0.0f);
    if (phi < -0.5f*float(pi) || phi > 0.5*float(pi)) approxUp = -approxUp;
    Vector3f rightVec = normalize(cross(viewVec, approxUp));
    Vector3f upUnrotated = cross(rightVec, viewVec);
    psi = atan2f(dot(rightVec, g_camUp), dot(upUnrotated, g_camUp));

    g_rotMat[0] = 1.0;
    g_rotMat[5] = 1.0;
    g_rotMat[10] = 1.0;
    g_rotMat[15] = 1.0;

    /* initialize GLUT */
    int argc = 1; char* argv = (char*)"";
    glutInit(&argc, &argv);
    glutInitWindowSize((GLsizei)g_width, (GLsizei)g_height);
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);
    glutInitWindowPosition(0, 0);
    g_window = glutCreateWindow("Embree");
    //if (g_fullscreen) {
    //exit(1);
    //  glutFullScreen();
    //}
    //glutSetCursor(GLUT_CURSOR_NONE);

    // @syoyo
    InitRenderConfig(g_renderConfig, g_width/2, g_height, g_stereoPixelMargin);
    PreparePostProcessShader(g_renderConfig);
    GenRenderToTexture(g_renderConfig);

    glutDisplayFunc(displayFunc);
    glutIdleFunc(idleFunc);
    glutKeyboardFunc(keyboardFunc);
    glutSpecialFunc(specialFunc);
    glutMouseFunc(clickFunc);
    glutMotionFunc(motionFunc);
    glutReshapeFunc(reshapeFunc);

    // force fullscreen.
    glutFullScreen();

    glutMainLoop();
  }
}
