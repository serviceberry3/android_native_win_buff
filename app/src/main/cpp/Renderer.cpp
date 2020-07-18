#include "Renderer.h"

//--------------------------------------------------------------------------------
// Teapot model data
//--------------------------------------------------------------------------------

//--------------------------------------------------------------------------------
// Constructor
//--------------------------------------------------------------------------------
Renderer::Renderer(int w, int h) {
    LOGI("Renderer calling Init");
    Init(w, h);
}

//--------------------------------------------------------------------------------
// Deconstructor
//--------------------------------------------------------------------------------
Renderer::~Renderer() {};

//error logging
static void printGLString(const char *name, GLenum s) {
    const char *v = (const char *) glGetString(s);
    LOGI("GL %s = %s\n", name, v);
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error = glGetError()) {
        LOGI("after %s() glError (0x%x)\n", op, error);
    }
}

auto gVertexShader =
        "attribute vec4 vPosition;\n"
        "void main() {\n"
        "  gl_Position = vPosition;\n"
        "}\n";

auto gFragmentShader =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
        "}\n";


//create and compile a single shader code
GLuint loadShader(GLenum shaderType, const char* pSource) {
    LOGI("Running glCreateShader...");
    GLuint shader = glCreateShader(shaderType);
    LOGI("glCreateShader done");

    if (shader) {
        LOGE("glCreateShader worked");
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
                    LOGE("Could not compile shader %d:\n%s\n",
                         shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    else {
        LOGE("glCreateShader failed");
    }
    return shader;
}

//link the shader program and attach the shaders
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    LOGI("Welcome to createProgram");
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);

    if (!vertexShader) {
        LOGE("Vertexshader NULL after loadShader");
        return 0;
    }

    LOGI("Running loadShader...");
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
            LOGI("linkStatus NOT GL_TRUE");
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    LOGE("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
        LOGI("createProgram all good");
    }
    return program;
}


static float triangleCoords[] = {   // in counterclockwise order:
        20.0f, 20.0f, 0.0f, // top
        15.0f, 0.0, 0.0f, // bottom left
        15.0f, 15.0, 0.0f  // bottom right
};


/*
static float triangleCoords[] = {   // in counterclockwise order:
        0.5f, 0.5f,
        0.0f, 0.0f,  //top, bottom left, bottom right
        1.0f, 0.0f
};
 */

/*
static float triangleCoords[] = {   // in counterclockwise order:
        0.0f, 0.25f,
        -0.5f, -0.25f,  //top, bottom left, bottom right
        0.5f, -0.25f
};
*/

bool Renderer::Init(int w, int h) {
    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Extensions", GL_EXTENSIONS);

    LOGI("Init(%d, %d)", w, h);

    //create an entire drawing program
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        LOGE("Could not create program.");
        return false;
    }

    LOGI("Attempting glGetAttribLocation");
    gvPositionHandle = glGetAttribLocation(gProgram, "vPosition");
    LOGI("glGetAttribLocation done");

    checkGlError("glGetAttribLocation");

    LOGI("glGetAttribLocation(\"vPosition\") = %d\n",
         gvPositionHandle);

    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    return true;
}

void Renderer::UpdateViewport() {
    // Init Projection matrices
    int32_t viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
}

void Renderer::Unload() {
    if (vbo_) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }

    if (ibo_) {
        glDeleteBuffers(1, &ibo_);
        ibo_ = 0;
    }

    if (shader_param_.program_) {
        glDeleteProgram(shader_param_.program_);
        shader_param_.program_ = 0;
    }
}

void Renderer::Update(float fTime) {
}

void Renderer::Render() {
    static float grey;

    grey += 0.01f;

    if (grey > 1.0f) {
        grey = 0.0f;
    }

    glClearColor(grey, grey, grey, 1.0f);

    checkGlError("glClearColor");

    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    checkGlError("glClear");

    glUseProgram(gProgram);

    checkGlError("glUseProgram");

    glVertexAttribPointer(gvPositionHandle, 2, GL_FLOAT, GL_FALSE, 0, triangleCoords);

    checkGlError("glVertexAttribPointer");

    glEnableVertexAttribArray(gvPositionHandle);

    checkGlError("glEnableVertexAttribArray");

    glDrawArrays(GL_TRIANGLES, 0, 3);

    checkGlError("glDrawArrays");
}