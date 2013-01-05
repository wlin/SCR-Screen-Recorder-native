#define LOG_NDEBUG 0
#define LOG_TAG "screenrec"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/log.h>

#include <media/mediarecorder.h>
#include <gui/SurfaceTextureClient.h>

static const char sVertexShader[] =
    "attribute vec4 vPosition;\n"
    "attribute vec2 texCoord; \n"
    "varying vec2 tc; \n"
    "void main() {\n"
    "  tc = texCoord;\n"
    "  gl_Position = mat4(0,1,0,0,  1,0,0,0, 0,0,-1,0,  0,0,0,1) * vPosition;\n"
    "}\n";

static const char sFragmentShader[] =
    "precision mediump float;\n"
    "uniform sampler2D textureSampler; \n"
    "varying vec2 tc; \n"
    "void main() {\n"
    //"  gl_FragColor = vec4(0.0, 1.0, 0, 1.0);\n"
    "  gl_FragColor.bgra = texture2D(textureSampler, tc); \n"
    "}\n";

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        ALOGI("after %s() glError (0x%x)\n", op, error);
    }
}

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    ALOGE("Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    ALOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

static EGLint eglConfigAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_RECORDABLE_ANDROID, EGL_TRUE,
            EGL_NONE };

static EGLint eglContextAttribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE };

EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
EGLSurface mEglSurface = EGL_NO_SURFACE;
EGLContext mEglContext = EGL_NO_CONTEXT;
EGLConfig mEglconfig;

GLuint mProgram;

int setupEgl() {
    ALOGV("setupEgl()");
    mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglGetError() != EGL_SUCCESS || mEglDisplay == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay() failed");
        return -1;
    }

    EGLint majorVersion;
    EGLint minorVersion;
    eglInitialize(mEglDisplay, &majorVersion, &minorVersion);
    if (eglGetError() != EGL_SUCCESS) {
        ALOGE("eglInitialize() failed");
        return -1;
    }

    EGLint numConfigs = 0;
    eglChooseConfig(mEglDisplay, eglConfigAttribs, &mEglconfig, 1, &numConfigs);
    if (eglGetError() != EGL_SUCCESS  || numConfigs < 1) {
        ALOGE("eglChooseConfig() failed");
        return -1;
    }
    mEglContext = eglCreateContext(mEglDisplay, mEglconfig, EGL_NO_CONTEXT, eglContextAttribs);
    if (eglGetError() != EGL_SUCCESS || mEglContext == EGL_NO_CONTEXT) {
        ALOGE("eglGetDisplay() failed");
        return -1;
    }
    return 0;
}

void tearDownEgl() {
    if (mEglContext != EGL_NO_CONTEXT) {
        eglDestroyContext(mEglDisplay, mEglContext);
    }
    if (mEglSurface != EGL_NO_SURFACE) {
         eglDestroySurface(mEglDisplay, mEglSurface);
    }
    if (mEglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(mEglDisplay);
    }
    if (eglGetError() != EGL_SUCCESS) {
        ALOGE("tearDownEgl() failed");
    }
}

namespace android {

sp<MediaRecorder> mr;
sp<SurfaceTextureClient> mSTC;
sp<ANativeWindow> mANW;

// Set up the MediaRecorder which runs in the same process as mediaserver
int setupMediaRecorder(int fd, int width, int height) {
    mr = new MediaRecorder();
    mr->setVideoSource(VIDEO_SOURCE_GRALLOC_BUFFER);
    mr->setOutputFormat(OUTPUT_FORMAT_MPEG_4);
    mr->setVideoEncoder(VIDEO_ENCODER_H264);
    mr->setOutputFile(fd, 0, 0);
    mr->setVideoSize(width, height);
    mr->setVideoFrameRate(30);
    mr->setParameters(String8("video-param-rotation-angle-degrees=90"));
    mr->setParameters(String8("video-param-encoding-bitrate=1000000"));
    mr->prepare();
    ALOGV("Starting MediaRecorder...");
    if (mr->start() != OK) {
        ALOGE("Error starting MediaRecorder");
        return -1;
    }

    sp<ISurfaceTexture> iST = mr->querySurfaceMediaSourceFromMediaServer();
    mSTC = new SurfaceTextureClient(iST);
    mANW = mSTC;

    mEglSurface = eglCreateWindowSurface(mEglDisplay, mEglconfig, mANW.get(), NULL);
    if (eglGetError() != EGL_SUCCESS || mEglSurface == EGL_NO_SURFACE) {
        ALOGE("eglCreateWindowSurface() failed");
        return -1;
    };

    eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext);
    if (eglGetError() != EGL_SUCCESS ) {
        ALOGE("eglMakeCurrent() failed");
        return -1;
    };
    return 0;
}

void tearDownMediaRecorder() {
    ALOGV("Stopping MediaRecorder...");
    mr->stop();
    ALOGV("Stopped");
    mr.clear();
    mSTC.clear();
    mANW.clear();
}

}

int main(int argc, char* argv[]) {
    printf("Screen Recorder\n");
    ALOGV("TEST");
    setupEgl();
    ALOGV("EGL initialized");
    tearDownEgl();
}



