/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

//BEGIN_INCLUDE(all)
#include <initializer_list>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <jni.h>
#include <cerrno>
#include <cassert>

//Android uses the OpenGL ES (GLES) API to render graphics. To create GLES contexts and provide a windowing system for GLES renderings,
// Android uses the EGL library. GLES calls render textured polygons, while EGL calls put renderings on screens
#include <EGL/egl.h>
#include <GLES/gl.h>
#include "Renderer.h"

#include <android/sensor.h>
#include <android/log.h>
#include <android_native_app_glue.h>

typedef uint16_t color_16bits_t;
typedef uint8_t  color_8bits_channel_t;
typedef uint16_t window_pixel_t;

//#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))
#define PIXEL_COLORS_MAX 4
#define PIXEL_COLORS_MAX_MASK 0b11

//quick function that takes some values r g b and manipulates them to get RGB565 result
#define make565(r,g,b) ( (color_16bits_t) ((r >> 3) << 11) | ((g >> 2) << 5)  | (b >> 3) )

#ifndef __cplusplus
    enum bool { false, true };
	typedef enum bool bool;
#endif

/**
 * Our saved state data (accelerometer reading)
 */
struct saved_state {
    float angle;
    int32_t x;
    int32_t y;
};


int frameNum = 0;
android_native_rect_t test_rect {300, 4000, 1200, 6000};
int dir = 0;

//Layer is the most important unit of composition. A layer is a combination of a surface and an instance of SurfaceControl

/*
static inline window_pixel_t * buffer_first_pixel_of_next_line
        (ANativeWindow_Buffer const * __restrict const buffer,
         window_pixel_t       const * __restrict const line_start)
{
    return (window_pixel_t *) (line_start + buffer->stride);
}
*/

static inline uint_fast32_t pixel_colors_next (uint_fast32_t current_index)
{
    return (rand() & PIXEL_COLORS_MAX_MASK);
}

static void fill_pixels(ANativeWindow_Buffer* buffer)
{

    //create array of 4 16-bit unsigned ints (will always be same value)
    static color_16bits_t const pixel_colors[PIXEL_COLORS_MAX] = {
            make565(255,  0,  0), //0b1111100000000000,
            make565(  0,255,  0),
            make565(  0,  0,255),
            make565(255,255,  0)
    };


    //Current pixel colors index

    //p_c is a result of bitwise AND of random integer with 0b11 (the max index). In other words, pick a random index into pixel_colors
    uint_fast32_t p_c = rand() & PIXEL_COLORS_MAX_MASK;

    color_16bits_t current_pixel_color;

    //pointer to buffer of uint16_t
    auto * current_pixel = (window_pixel_t *)buffer->bits;

    //number of pixels per line
    uint_fast32_t const line_width  = buffer->width;

    //stride
    uint_fast32_t const line_stride = buffer->stride;

    //number of pixel lines we have available
    uint_fast32_t n_lines = buffer->height;

    //stops when n_lines is at 0 (have iterated through every line
    while (n_lines--) {
        //pointer to start of the current pixel line (starts at beginning of buffer->bits)
        window_pixel_t const* current_line_start = current_pixel;

        //pointer to the last pixel in the line
        window_pixel_t const* last_pixel_of_the_line = current_line_start + line_width;

        //current_pixel_color = (n_lines % 1 == 0) ? 1200 : 5000;

        //get the desired color, choosing randomly from the 4 available
        //CHANGED to always pick red for now
        current_pixel_color = pixel_colors[0];

        if (n_lines % 20==0) {
            //write all pixels in the line
            while ((uintptr_t)current_pixel <= (uintptr_t)last_pixel_of_the_line-1080) {
                *current_pixel = current_pixel_color;
                current_pixel++;
            }

            //switch over to green about halfway across screen
            current_pixel_color = pixel_colors[1];

            while ((uintptr_t)current_pixel <= (uintptr_t)last_pixel_of_the_line) {
                *current_pixel = current_pixel_color;
                current_pixel++;
            }
        }

        //change the random index (color selector)
        p_c = pixel_colors_next(p_c);

        //move to next pixel line
        current_pixel = (unsigned short *) (current_line_start + (line_stride));
    }
}

/**
 * Shared state for our app.
 */
struct engine {
    struct android_app* app;

    Renderer* renderer;

    ASensorManager* sensorManager;
    const ASensor* accelerometerSensor;
    ASensorEventQueue* sensorEventQueue;

    int animating;

    //display is another important unit of composition. A system can have multiple displays and displays can be added or
    // removed during normal system operations. Displays are added/removed at request of the HWC or the framework.
    EGLDisplay display;

    //Before you draw with GLES, need to create GL context. In EGL, this means creating EGLContext and EGLSurface.

    //EGLSurface can be off-screen buffer allocated by EGL, called a pbuffer, or window allocated by the operating system.
    //Only one EGLSurface can be associated with a surface at a time (you can have only one producer connected to a BufferQueue),
    // but if you destroy the EGLSurface it disconnects from the BufferQueue and lets something else connect.

    //A given thread can switch between multiple EGLSurfaces by changing what's CURRENT. An EGLSurface must be CURRENT on
    // only one thread at a time.
    //
    //EGL isn't another aspect of a surface (like SurfaceHolder). EGLSurf is a related but independent concept. You can draw on an
    //EGLSurface that isn't backed by a surface, and can use a surface without EGL. EGLSurface just provides GLES with place to draw
    EGLSurface surface;
    EGLContext context;

    int32_t initial_window_format;

    int32_t width;
    int32_t height;
    struct saved_state state;
};

static inline bool engine_have_a_window
        (struct engine const * __restrict const engine)
{
    return engine->app->window != NULL;
}

static inline void engine_term_display
        (struct engine * __restrict const engine)
{
    engine->animating = 0;
}


void eglErrorString(EGLint error)
{
    switch(error)
    {
        case EGL_SUCCESS: LOGI("ERROR AFTER CREATEWINDSURF: success"); return;
        case EGL_NOT_INITIALIZED: LOGI("ERROR AFTER CREATEWINDSURF: not initialized"); return;
        case EGL_BAD_ACCESS: LOGI("ERROR AFTER CREATEWINDSURF: bad access"); return;
        case EGL_BAD_ALLOC: LOGI("ERROR AFTER CREATEWINDSURF: bad alloc"); return;
        case EGL_BAD_ATTRIBUTE: LOGI("ERROR AFTER CREATEWINDSURF: bad attribute"); return;
        case EGL_BAD_CONTEXT: LOGI("ERROR AFTER CREATEWINDSURF: bad context"); return;
        case EGL_BAD_CONFIG: LOGI("ERROR AFTER CREATEWINDSURF: bad config");return;
        case EGL_BAD_CURRENT_SURFACE: LOGI("ERROR AFTER CREATEWINDSURF: bad current surface");return;
        case EGL_BAD_DISPLAY: LOGI("ERROR AFTER CREATEWINDSURF: bad display");return;
        case EGL_BAD_SURFACE: LOGI("ERROR AFTER CREATEWINDSURF: bad surface");return;
        case EGL_BAD_MATCH: LOGI("ERROR AFTER CREATEWINDSURF: bad match");return;
        case EGL_BAD_PARAMETER: LOGI("ERROR AFTER CREATEWINDSURF: bad parameter");return;
        case EGL_BAD_NATIVE_PIXMAP: LOGI("ERROR AFTER CREATEWINDSURF: bad native pixmap");return;
        case EGL_BAD_NATIVE_WINDOW: LOGI("ERROR AFTER CREATEWINDSURF: bad native window");return;
        case EGL_CONTEXT_LOST: LOGI("ERROR AFTER CREATEWINDSURF: lost context");return;
    }

    LOGI("CURRENT ERROR: %d", error);
}


/**
 * Initialize an EGL context for the current display.
 */
 /*
static int engine_init_display(struct engine* engine) {
    LOGI("engine_init_display called");
    // initialize OpenGL ES and EGL


     //specify the attributes of the desired configuration.
     //Below, we select an EGLConfig with at least 8 bits per color
     //component compatible with on-screen windows

    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_NONE
    };
    EGLint w, h, format;
    EGLint numConfigs;
    EGLConfig config = nullptr;
    EGLSurface surface;
    EGLContext context;

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

    eglInitialize(display, nullptr, nullptr);

    //Here, the application chooses the configuration it desires.
     //find the best match if possible based on attribs list, otherwise use the very first one

    eglChooseConfig(display, attribs, nullptr,0, &numConfigs);

    //unique_ptr is a smart pointer that owns and manages another object through a pointer and disposes of that object
    //when the unique_ptr goes out of scope.
    std::unique_ptr<EGLConfig[]> supportedConfigs(new EGLConfig[numConfigs]);
    assert(supportedConfigs);
    eglChooseConfig(display, attribs, supportedConfigs.get(), numConfigs, &numConfigs);
    assert(numConfigs);

    auto i = 0;

    for (; i < numConfigs; i++) {
        auto& cfg = supportedConfigs[i];
        EGLint r, g, b, d;

        //get color data through EGLints from the display
        if (eglGetConfigAttrib(display, cfg, EGL_RED_SIZE, &r)   &&
            eglGetConfigAttrib(display, cfg, EGL_GREEN_SIZE, &g) &&
            eglGetConfigAttrib(display, cfg, EGL_BLUE_SIZE, &b)  &&
            eglGetConfigAttrib(display, cfg, EGL_DEPTH_SIZE, &d) &&
            r == 8 && g == 8 && b == 8 && d == 0 ) {

            config = supportedConfigs[i];
            break;
        }
    }
    if (i == numConfigs) {
        config = supportedConfigs[0];
    }

    if (config == nullptr) {
        LOGW("Unable to initialize EGLConfig");
        return -1;
    }


    EGLint AttribList[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE
    };

    //EGL_NATIVE_VISUAL_ID is an attribute of the EGLConfig that is
     //guaranteed to be accepted by ANativeWindow_setBuffersGeometry().
     //As soon as we picked a EGLConfig, we can safely reconfigure the
     //ANativeWindow buffers to match, using EGL_NATIVE_VISUAL_ID.
    eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);

    EGLint windowAttributes[] = {
            EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
            EGL_NONE
    };

            // Calling eglCreateWindowSurface() creates EGL window surfaces. eglCreateWindowSurface() takes a window
    // object as an argument, which on Android is a SURFACE (aka ANativeWindow.cpp). A SURFACE is the "PRODUCER" side of a BufferQueue.
    // "CONSUMERS," whichare SurfaceView, SurfaceTexture, TextureView, or ImageReader, create surfaces. When you call eglCreateWindowSurface(),
    // EGL creates new EGLSurface object and connects it to "PRODUCER" interface of window object's BufferQueue. From
    // that point onward, rendering to that EGLSurface results in buffer being dequeued, rendered into, and queued for use by consumer

    //native_window_set_buffers_user_dimensions(engine->app->window, 1080*3, 2280*3);
    //@param engine->app->window a pointer to an ANativeWindow
    surface = eglCreateWindowSurface(display, config, engine->app->window, windowAttributes);
    eglErrorString(eglGetError());

    context = eglCreateContext(display, config, nullptr, AttribList);

    if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE) {
        LOGW("Unable to eglMakeCurrent");
        return -1;
    }

    //get width and height data of the surface through more EGLints w and h
    eglQuerySurface(display, surface, EGL_WIDTH, &w);
    eglQuerySurface(display, surface, EGL_HEIGHT, &h);

    w = 1080*3;
    h = 2280*3;

    engine->display = display;
    engine->context = context;
    engine->surface = surface;
    engine->width = 1080*3;
    engine->height = 2280*3;
    engine->state.angle = 0;

    // Check openGL on the system
    auto opengl_info = {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_EXTENSIONS};
    for (auto name : opengl_info) {
        auto info = glGetString(name);
        LOGI("OpenGL Info: %s", info);
    }


    //Initialize GL state.

    //glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    //glEnable(GL_CULL_FACE);
    //glShadeModel(GL_SMOOTH);
    //glDisable(GL_DEPTH_TEST);

    frameNum = 0;
    LOGI("Finished initDisplay");
    return 0;
}
*/



//draw a frame
static void engine_draw_frame(struct engine* engine) {
    /*
    if (engine->display == nullptr) {
        // No display.
        return;
    }

    // Just fill the screen with a color.
    //glClearColor(((float)engine->state.x)/engine->width, engine->state.angle,((float)engine->state.y)/engine->height, 1);
    //glClearColor(220, 0, 0, 0);


    if (frameNum==0) {
        //first frame, do rendering
        glClear(GL_COLOR_BUFFER_BIT);

        //LOGI("Attempting render");

        //invokes the GPU ONCE to do rendering/shading
        engine->renderer->Render();

        //LOGI("Finsihed render");
    }

    else if (dir == 0) {
        //adjust crop
        test_rect.top+=30;
        test_rect.bottom+=30;
        native_window_set_crop(engine->app->window, &test_rect);
        if (test_rect.top>=4400) {
            dir = 1;
        }
    }

    else {
        //dir = 1
        test_rect.top-=30;
        test_rect.bottom-=30;
        native_window_set_crop(engine->app->window, &test_rect);
        if (test_rect.top<=3800) {
            dir = 0;
        }
    }
    eglSwapBuffers(engine->display, engine->surface);
     */


    ANativeWindow_Buffer buffer;

    if (!engine_have_a_window(engine))
    {
        LOGI( "The engine doesn't have a window !?\n");
        goto draw_frame_end;
    }


    if (ANativeWindow_lock(engine->app->window, &buffer, NULL) < 0)
    {
        LOGI("Could not lock the window... :C\n");
        goto draw_frame_end;
    }

    fill_pixels(&buffer);
    ANativeWindow_unlockAndPost(engine->app->window);

    //LOGI("Frame number %d", frameNum);
    frameNum++;

    draw_frame_end:
    return;
}

/*
//destroy EGL context currently associated with the display
static void engine_term_display(struct engine* engine) {
    if (engine->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->context != EGL_NO_CONTEXT) {
            eglDestroyContext(engine->display, engine->context);
        }
        if (engine->surface != EGL_NO_SURFACE) {
            eglDestroySurface(engine->display, engine->surface);
        }
        eglTerminate(engine->display);
    }
    engine->animating = 0;
    engine->display = EGL_NO_DISPLAY;
    engine->context = EGL_NO_CONTEXT;
    engine->surface = EGL_NO_SURFACE;
}
  */

//process the next input event
static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
    /*
    auto* engine = (struct engine*)app->userData;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        //set animating to true

        //IGNORE TOUCH EVENTS FOR NOW

        engine->animating = 1;

        engine->state.x = AMotionEvent_getX(event, 0);

        engine->state.y = AMotionEvent_getY(event, 0);

        return 1;
    }
    return 0;
     */

    auto * const engine =
            (struct engine *) app->userData;

    int32_t const current_event_type =
            AInputEvent_getType(event);

    if (current_event_type == AINPUT_EVENT_TYPE_MOTION) {
        engine->animating = 1;
        return 1;
    } else if (current_event_type == AINPUT_EVENT_TYPE_KEY) {
        LOGI("Key event: action=%d keyCode=%d metaState=0x%x",
            AKeyEvent_getAction(event),
            AKeyEvent_getKeyCode(event),
            AKeyEvent_getMetaState(event));
    }

    return 0;
}

//process the next main command
static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
    /*
    auto* engine = (struct engine*)app->userData;
    switch (cmd) {
        case APP_CMD_SAVE_STATE:
            // The system has asked us to save our current state.  Do so.
            engine->app->savedState = malloc(sizeof(struct saved_state));
            *((struct saved_state*)engine->app->savedState) = engine->state;
            engine->app->savedStateSize = sizeof(struct saved_state);
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            if (engine->app->window != nullptr) {
                engine_init_display(engine);
                engine->renderer = new Renderer(1080, 2280);
                LOGI("New REnderer created");
                engine_draw_frame(engine);
            }
            LOGI("APP_CMD_INIT_WINDOW case");
            LOGI("DEREF");
            LOGI("OVER");
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.


            if (engine->renderer) {
                auto* pRenderer = reinterpret_cast<Renderer*>(engine->renderer);
                engine->renderer = nullptr;
                delete pRenderer;
            }


            engine_term_display(engine);
            break;
        case APP_CMD_GAINED_FOCUS:
            // When our app gains focus, we start monitoring the accelerometer.
            if (engine->accelerometerSensor != nullptr) {
                ASensorEventQueue_enableSensor(engine->sensorEventQueue,
                                               engine->accelerometerSensor);
                // We'd like to get 60 events per second (in us).
                ASensorEventQueue_setEventRate(engine->sensorEventQueue,
                                               engine->accelerometerSensor,
                                               (1000L/60)*1000);
            }
            break;
        case APP_CMD_LOST_FOCUS:
            // When our app loses focus, we stop monitoring the accelerometer.
            // This is to avoid consuming battery while not being used.
            if (engine->accelerometerSensor != nullptr) {
                ASensorEventQueue_disableSensor(engine->sensorEventQueue,
                                                engine->accelerometerSensor);
            }
            // Also stop animating.
            engine->animating = 0;
            engine_draw_frame(engine);
            break;
        default:
            break;
    }
     */

    struct engine * __restrict const engine =
            (struct engine *) app->userData;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (engine_have_a_window(engine))
            {

                engine->initial_window_format =
                        ANativeWindow_getFormat(app->window);

                ANativeWindow_setBuffersGeometry(app->window,
                                                 ANativeWindow_getWidth(app->window),
                                                 ANativeWindow_getHeight(app->window),
                                                 WINDOW_FORMAT_RGB_565);

                engine_draw_frame(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            engine_term_display(engine);

            ANativeWindow_setBuffersGeometry(app->window,
                                             ANativeWindow_getWidth(app->window),
                                             ANativeWindow_getHeight(app->window),
                                             engine->initial_window_format);

            break;
        case APP_CMD_LOST_FOCUS:
            engine->animating = 0;
            engine_draw_frame(engine);
            break;
    }
}

/*
 * AcquireASensorManagerInstance(void)
 *    Workaround ASensorManager_getInstance() deprecation false alarm
 *    for Android-N and before, when compiling with NDK-r15
 */
#include <dlfcn.h>
ASensorManager* AcquireASensorManagerInstance(android_app* app) {

  if(!app)
    return nullptr;

  typedef ASensorManager *(*PF_GETINSTANCEFORPACKAGE)(const char *name);
  void* androidHandle = dlopen("libandroid.so", RTLD_NOW);
  auto getInstanceForPackageFunc = (PF_GETINSTANCEFORPACKAGE)
      dlsym(androidHandle, "ASensorManager_getInstanceForPackage");
  if (getInstanceForPackageFunc) {
    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);

    jclass android_content_Context = env->GetObjectClass(app->activity->clazz);
    jmethodID midGetPackageName = env->GetMethodID(android_content_Context,
                                                   "getPackageName",
                                                   "()Ljava/lang/String;");
    auto packageName= (jstring)env->CallObjectMethod(app->activity->clazz,
                                                        midGetPackageName);

    const char *nativePackageName = env->GetStringUTFChars(packageName, nullptr);
    ASensorManager* mgr = getInstanceForPackageFunc(nativePackageName);
    env->ReleaseStringUTFChars(packageName, nativePackageName);
    app->activity->vm->DetachCurrentThread();
    if (mgr) {
      dlclose(androidHandle);
      return mgr;
    }
  }

  typedef ASensorManager *(*PF_GETINSTANCE)();
  auto getInstanceFunc = (PF_GETINSTANCE)
      dlsym(androidHandle, "ASensorManager_getInstance");
  // by all means at this point, ASensorManager_getInstance should be available
  assert(getInstanceFunc);
  dlclose(androidHandle);

  return getInstanceFunc();
}


/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(struct android_app* state) {
    /*
    struct engine engine{};

    //set all engine fields to 0
    memset(&engine, 0, sizeof(engine));


    state->userData = &engine;
    state->onAppCmd = engine_handle_cmd;
    state->onInputEvent = engine_handle_input;
    engine.app = state;

    // Prepare to monitor accelerometer
    engine.sensorManager = AcquireASensorManagerInstance(state);
    engine.accelerometerSensor = ASensorManager_getDefaultSensor(
                                        engine.sensorManager,
                                        ASENSOR_TYPE_ACCELEROMETER);
    engine.sensorEventQueue = ASensorManager_createEventQueue(
                                    engine.sensorManager,
                                    state->looper, LOOPER_ID_USER,
                                    nullptr, nullptr);

    if (state->savedState != nullptr) {
        // We are starting with a previous saved state; restore from it.
        engine.state = *(struct saved_state*)state->savedState;
    }

    // loop waiting for stuff to do.
    while (true) {
        // Read all pending events.
        int ident;
        int events;
        struct android_poll_source* source;

        // If not animating, we will block forever waiting for events.
        // If animating, we loop until all events are read, then continue
        // to draw the next frame of animation.
        while ((ident=ALooper_pollAll(engine.animating ? 0 : -1, nullptr, &events,
                                      (void**)&source)) >= 0) {

            // Process this event.
            if (source != nullptr) {
                source->process(state, source);
            }

            // If a sensor has data, process it now.
            if (ident == LOOPER_ID_USER) {
                if (engine.accelerometerSensor != nullptr) {
                    ASensorEvent event;
                    while (ASensorEventQueue_getEvents(engine.sensorEventQueue, &event, 1) > 0) {
                        //LOGI("accelerometer: x=%f y=%f z=%f", event.acceleration.x, event.acceleration.y, event.acceleration.z);
                    }
                }
            }

            // Check if we are exiting.
            if (state->destroyRequested != 0) {
                engine_term_display(&engine);
                return;
            }
        }

        if (engine.animating) {
            // Done with events; draw next animation frame.
            engine.state.angle += .01f;
            if (engine.state.angle > 1) {
                engine.state.angle = 0;
            }

            // Drawing is throttled to the screen update rate, so there
            // is no need to do timing here.
            engine_draw_frame(&engine);
        }
    }
     */

    struct engine engine = {0};

    state->userData = &engine;
    state->onAppCmd = engine_handle_cmd;
    state->onInputEvent = engine_handle_input;
    engine.app = state;

    // loop waiting for stuff to do.

    while (1) {
        // Read all pending events.
        int ident;
        int events;
        struct android_poll_source* source;

        // If not animating, we will block forever waiting for events.
        // If animating, we loop until all events are read, then continue
        // to draw the next frame of animation.
        while (
                (ident=ALooper_pollAll(
                        engine.animating ? 0 : -1,
                        NULL, &events, (void **) &source))
                >= 0)
        {

            // Process this event.
            if (source != NULL) {
                source->process(state, source);
            }

            // Check if we are exiting.
            if (state->destroyRequested != 0) {
                LOGI("Engine thread destroy requested!");
                engine_term_display(&engine);
                return;
            }
        }

        if (engine.animating) {
            engine_draw_frame(&engine);
        }
    }
}
//END_INCLUDE(all)
