#include <initializer_list>
#include <memory>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <jni.h>
#include <cerrno>
#include <cassert>
#include <iostream>

//Android uses the OpenGL ES (GLES) API to render graphics. To create GLES contexts and provide a windowing system for GLES renderings,
//Android uses the EGL library. GLES calls render textured polygons, while EGL calls put renderings on screens
#include <EGL/egl.h>
#include <GLES/gl.h>
#include "Renderer.h"
#include "glyphs.h"
#include "json.hpp"

#include <android/sensor.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <android/bitmap.h>
#include <fstream>

typedef uint16_t color_16bits_t;
typedef uint8_t  color_8bits_channel_t;
typedef uint16_t window_pixel_t;

//#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))
#define PIXEL_COLORS_MAX 5
#define PIXEL_COLORS_MAX_MASK 0b11

#define ANDROID_NATIVE_MAKE_CONSTANT(a,b,c,d) (((unsigned)(a)<<24)|((unsigned)(b)<<16)|((unsigned)(c)<<8)|(unsigned)(d))
#define ANDROID_NATIVE_WINDOW_MAGIC ANDROID_NATIVE_MAKE_CONSTANT('_','w','n','d')
#define ANDROID_NATIVE_BUFFER_MAGIC ANDROID_NATIVE_MAKE_CONSTANT('_','b','f','r')

//#define OG

//quick function that takes some values r g b and manipulates them to get RGB565 result
#define make565(r,g,b) ( (color_16bits_t) ((r >> 3) << 11) | ((g >> 2) << 5)  | (b >> 3) )

#ifndef __cplusplus
    enum bool { false, true };
	typedef enum bool bool;
#endif

using json = nlohmann::json;


/**
 * Our saved state data (accelerometer reading)
 */
struct saved_state {
    float angle;
    int32_t x;
    int32_t y;
};


//TAKE DEFINITIONS FROM system/window.h TO AVOID COMPILER ERRORS------------------------------------------------------------------------------------------------
typedef struct android_native_rect_t
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} android_native_rect_t;



typedef struct android_native_base_t
{
    /* a magic value defined by the actual EGL native type */
    int magic;
    /* the sizeof() of the actual EGL native type */
    int version;
    void* reserved[4];
    /* reference-counting interface */
    void (*incRef)(struct android_native_base_t* base);
    void (*decRef)(struct android_native_base_t* base);
} android_native_base_t;


coord text_coords[63659] = {};

struct ANativeWindow
{
#ifdef __cplusplus
    //constructor
    ANativeWindow(): flags(0), minSwapInterval(0), maxSwapInterval(0), xdpi(0), ydpi(0)
    {
        common.magic = ANDROID_NATIVE_WINDOW_MAGIC;
        common.version = sizeof(ANativeWindow);
        memset(common.reserved, 0, sizeof(common.reserved));
    }

    /* Implement the methods that sp<ANativeWindow> expects so that it
       can be used to automatically refcount ANativeWindow's. */
    void incStrong(const void* id) const {
        common.incRef(const_cast<android_native_base_t*>(&common));
    }
    void decStrong(const void* id) const {
        common.decRef(const_cast<android_native_base_t*>(&common));
    }
#endif
    struct android_native_base_t common;
    /* flags describing some attributes of this surface or its updater */
    const uint32_t flags;
    /* min swap interval supported by this updated */
    const int   minSwapInterval;
    /* max swap interval supported by this updated */
    const int   maxSwapInterval;
    /* horizontal and vertical resolution in DPI */
    const float xdpi;
    const float ydpi;
    /* Some storage reserved for the OEM's driver. */
    intptr_t    oem[4];
    /*
     * Set the swap interval for this surface.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*setSwapInterval)(struct ANativeWindow* window,
                               int interval);
    /*
     * Hook called by EGL to acquire a buffer. After this call, the buffer
     * is not locked, so its content cannot be modified. This call may block if
     * no buffers are available.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*dequeueBuffer)(struct ANativeWindow* window,
                             struct ANativeWindowBuffer** buffer);
    /*
     * hook called by EGL to lock a buffer. This MUST be called before modifying
     * the content of a buffer. The buffer must have been acquired with
     * dequeueBuffer first.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*lockBuffer)(struct ANativeWindow* window,
                          struct ANativeWindowBuffer* buffer);
    /*
     * Hook called by EGL when modifications to the render buffer are done.
     * This unlocks and post the buffer.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     *
     * Buffers MUST be queued in the same order than they were dequeued.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*queueBuffer)(struct ANativeWindow* window,
                           struct ANativeWindowBuffer* buffer);
    /*
     * hook used to retrieve information about the native window.
     *
     * Returns 0 on success or -errno on error.
     */
    int     (*query)(const struct ANativeWindow* window,
                     int what, int* value);
    /*
     * hook used to perform various operations on the surface.
     * (*perform)() is a generic mechanism to add functionality to
     * ANativeWindow while keeping backward binary compatibility.
     *
     * DO NOT CALL THIS HOOK DIRECTLY.  Instead, use the helper functions
     * defined below.
     *
     *  (*perform)() returns -ENOENT if the 'what' parameter is not supported
     *  by the surface's implementation.
     *
     * The valid operations are:
     *     NATIVE_WINDOW_SET_USAGE
     *     NATIVE_WINDOW_CONNECT               (deprecated)
     *     NATIVE_WINDOW_DISCONNECT            (deprecated)
     *     NATIVE_WINDOW_SET_CROP
     *     NATIVE_WINDOW_SET_BUFFER_COUNT
     *     NATIVE_WINDOW_SET_BUFFERS_GEOMETRY  (deprecated)
     *     NATIVE_WINDOW_SET_BUFFERS_TRANSFORM
     *     NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP
     *     NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS
     *     NATIVE_WINDOW_SET_BUFFERS_FORMAT
     *     NATIVE_WINDOW_SET_SCALING_MODE
     *     NATIVE_WINDOW_LOCK                   (private)
     *     NATIVE_WINDOW_UNLOCK_AND_POST        (private)
     *     NATIVE_WINDOW_API_CONNECT            (private)
     *     NATIVE_WINDOW_API_DISCONNECT         (private)
     *
     */
    int     (*perform)(struct ANativeWindow* window,
                       int operation, ... );
    /*
     * Hook used to cancel a buffer that has been dequeued.
     * No synchronization is performed between dequeue() and cancel(), so
     * either external synchronization is needed, or these functions must be
     * called from the same thread.
     *
     * The window holds a reference to the buffer between dequeueBuffer and
     * either queueBuffer or cancelBuffer, so clients only need their own
     * reference if they might use the buffer after queueing or canceling it.
     * Holding a reference to a buffer after queueing or canceling it is only
     * allowed if a specific buffer count has been set.
     */
    int     (*cancelBuffer)(struct ANativeWindow* window,
                            struct ANativeWindowBuffer* buffer);
    void* reserved_proc[2];
};
//END COPIED DECLARATIONS--------------------------------------------------------------------------------------------------------------

//create array of 4 16-bit unsigned ints (will always be same value)
static color_16bits_t const pixel_colors[PIXEL_COLORS_MAX] = {
        make565(255,  0,  0), //0b1111100000000000,  //pure red
        make565(  0,255,  0), //pure green
        make565(  0,  0,255), //pure blue
        make565(255,255,  0),
        make565(255, 255, 255),
};

int frameNum = 0;

//test cropping rectangle for screen
android_native_rect_t original {0, 0, 1080, 2280};
android_native_rect_t test_rect {0, 0, 2280, 1080};

//oscillation direction
int dir = 0;


//we want to start the buffer
int oscillator = 0;


window_pixel_t* large_buff; //2462400

//Layer is the most important unit of composition. A layer is a combination of a surface and an instance of SurfaceControl

/*
static inline window_pixel_t * buffer_first_pixel_of_next_line
        (ANativeWindow_Buffer const * __restrict const buffer,
         window_pixel_t       const * __restrict const line_start)
{
    return (window_pixel_t *) (line_start + buffer->stride);
}
*/

/*
static inline uint_fast32_t pixel_colors_next (uint_fast32_t current_index)
{
    return (rand() & PIXEL_COLORS_MAX_MASK);
}*/

static void get_txt_coords() {
    std::ifstream ifs("/system/files/text_coords.json");
    json jf = json::parse(ifs);

    LOGI("Size is %d\n", (int)jf.size());

    for (int i = 0; i < jf.size(); i++) {
        text_coords[i].x = (int)jf[i]["x"];
        text_coords[i].y = (int)jf[i]["y"];
    }

    LOGI("JSON Success\n");
}


//draw some text on the background
//draw some text on the background
//@param x_off - x offset from left of screen where we want to start drawing text
//@param y_off - y offset from top of screen where we want to start drawing text
static void drawText(int x_off, int y_off, color_16bits_t text_color, window_pixel_t* bits, int32_t stride) {
    int initial_adjust = 4452660;

    initial_adjust -= ((y_off - 1) * stride) + x_off;

    //draw text
    int this_pix;
    for (auto & text_coord : text_coords) {
        this_pix = ((text_coord.y - 1) * stride + text_coord.x) - initial_adjust; //first would be 4719600, which is way too big 4608 was 2176
        bits[this_pix] = text_color;
    }
}

#ifdef OG
//the coord list comes from the basic pixel coordinates found by
static void fill_pixels(ANativeWindow_Buffer* buffer)
{

    LOGI("Mem address of large_buff is %p", large_buff);

    //create array of 4 16-bit unsigned ints (will always be same value)
    static color_16bits_t const pixel_colors[PIXEL_COLORS_MAX] = {
            make565(255,  0,  0), //0b1111100000000000,  //pure red
            make565(  0,255,  0), //pure green
            make565(  0,  0,255), //pure blue
            make565(255,255,  0)
    };

    //NOTE***: each pixel takes up 2 bytes in memory, so we need a memory array of 2 * 1080 * 2280

    //Current pixel colors index

    //p_c is a result of bitwise AND of random integer with 0b11 (the max index). In other words, pick a random index into pixel_colors
    //uint_fast32_t p_c = rand() & PIXEL_COLORS_MAX_MASK;

    color_16bits_t current_pixel_color;


    //pointer to buffer of uint16_t
    auto* current_pixel = (window_pixel_t *)buffer->bits;

    LOGI("current_pixel (or initial buffer->bits) address is %p", current_pixel);


    //number of pixels per line
    uint_fast32_t const line_width = buffer->width;

    //stride
    uint_fast32_t const line_stride = buffer->stride;

    //number of pixel lines we have available
    uint_fast32_t n_lines = buffer->height;


    LOGI("Num lines is %d, width of lines is %d, stride is %d", (int)n_lines, (int)line_width, (int)line_stride);


    //EXPERIMENTAL: DOESN'T WORK.

    //Right off, I've noticed something interesting about this. Above, we set current_pixel to point to the address of buffer->bits. So now when we set buffer->bits to point
    //to large_buff, current_pixel remains at the original address of buffer->bits, while buffer->bits address does change to the address of large_buff (I checked this with the
    //logging). So in the while loop below, we're writing pixels at the original address of buffer->bits. And although we've supposedly changed the address of buffer->bits
    //to point to large_buff, that doesn't hold up once ANativeWindow_unlockAndPost() is called: what we see posted to the display queue is still what's written in the while loop
    //below.

    //So in conclusion, apparently we can't just set buffer.bits to some mem we allocated on the heap in this app. That doesn't do anything once the ANativeWindow
    //is pushed to the display. We need to do some diggin in the Android source to find a way to actually set that pointer. Then we could render even faster by having all the
    //pixels pre-rendered and just modifying the address.
    //buffer->bits = large_buff;

    LOGI("After setting buffer->bits = large_buff, current_pixel address is now %p, and buffer->bits is %p", current_pixel, buffer->bits);


    //stops when n_lines is at 0 (have iterated through every line
    while (n_lines--) { //starts at 2280
        //pointer to start of the current pixel line (starts at beginning of buffer->bits)
        window_pixel_t const* current_line_start = current_pixel;

        //pointer to the last pixel in the line
        window_pixel_t const* last_pixel_of_the_line = current_line_start + line_width;

        //current_pixel_color = (n_lines % 1 == 0) ? 1200 : 5000;

        //get the desired color, choosing randomly from the 4 available
        //CHANGED to always pick red for now
        current_pixel_color = pixel_colors[0];

        /*
        ///////////////////
        if (n_lines >= 1000 && n_lines <= 1300) {
            while (((uintptr_t) current_pixel <= (uintptr_t) last_pixel_of_the_line - 800)) {
                if ((uintptr_t) current_pixel >= (uintptr_t) current_line_start + 800) {
                    *current_pixel = current_pixel_color;
                }
                current_pixel++;
            }
        }
        /////////////////*/

        if (n_lines % 20 == 0) {
            //write first 1080 bytes in the line (5040 pixels)
            while ((uintptr_t)current_pixel <= (uintptr_t)last_pixel_of_the_line - 1080) {
                *current_pixel = current_pixel_color;

                //since current_pixel is an uint16_t ptr (2 bytes), this advances the read by 2 bytes
                current_pixel++;
            }

            //switch over to green about halfway across screen
            current_pixel_color = pixel_colors[1];

            //write second 1080 bytes in the line (5040 pixels)
            while ((uintptr_t)current_pixel <= (uintptr_t)last_pixel_of_the_line) {
                *current_pixel = current_pixel_color;

                //since current_pixel is an uint16_t ptr (2 bytes), this advances the read by 2 bytes
                current_pixel++;
            }
        }


        //change the random index (color selector)
        //p_c = pixel_colors_next(p_c);

        //move to next pixel line. Unsigned short is 2 bytes, line_stride is 1088. Since current_line_start is uin16_t ptr, adding 1088 advances the read by 1088*2 bytes,
        //bringing us to the first pixel in the next line
        current_pixel = (unsigned short *) (current_line_start + (line_stride));
    }


    //copy our pixels from large_buff over to the bits of the window
    memcpy(buffer->bits, large_buff + oscillator, sizeof(window_pixel_t) * 1080 * 2280);

    //bounce the red box back and forth across the screen
    if (dir == 0) {
        oscillator += 50;
    }
    else {
        oscillator -= 50;
    }

    if (oscillator == 750 && dir==0) {
        dir = 1;
    }
    else if (oscillator==0 && dir==1) {
        dir = 0;
    }
}
#endif

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
    //removed during normal system operations. Displays are added/removed at request of the HWC or the framework.
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

static inline bool engine_have_a_window (struct engine const * __restrict const engine)
{
    return engine->app->window != nullptr;
}

//terminate the display
static inline void engine_term_display (struct engine * __restrict const engine)
{
    //no longer animating
    engine->animating = 0;
}

void produce_txt_pixels() {

};


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

 /*
 void drawBitmap(JNIEnv *env, jobject obj, jobject surface, jobject bitmap) {
     //Get the information of the bitmap, such as width and height
     AndroidBitmapInfo info;

     if (AndroidBitmap_getInfo(env, bitmap, &info) < 0) {
         LOGI("java/lang/RuntimeException: unable to get bitmap info");
         return;
     }

     char *data = nullptr;

     //Get the native pointer corresponding to the bitmap
     if (AndroidBitmap_lockPixels(env, bitmap, (void **) &data) < 0) {
         LOGI("java/lang/RuntimeException: unable to lock pixels");
         return;
     }
     if (AndroidBitmap_unlockPixels(env, bitmap) < 0) {
         LOGI("java/lang/RuntimeException: unable to unlock pixels");
         return;
     }

     //Get the target surface
     ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
     if (window == nullptr) {
         LOGI("java/lang/RuntimeException: unable to get native window");
         return;
     }

     //Here is set to RGBA way, a total of 4 bytes 32 bits
     int32_t result = ANativeWindow_setBuffersGeometry(window, info.width, info.height,WINDOW_FORMAT_RGBA_8888);

     if (result < 0) {
         LOGI("java/lang/RuntimeException: unable to set buffers geometry");

         //release the window
         ANativeWindow_release(window);
         window = nullptr;
         return;
     }
     ANativeWindow_acquire(window);

     ANativeWindow_Buffer buffer;

     //Lock the drawing surface of the window
     if (ANativeWindow_lock(window, &buffer, nullptr) < 0) {
         LOGI("java/lang/RuntimeException: unable to lock native window");

         //release the window
         ANativeWindow_release(window);
         window = nullptr;
         return;
     }

     //Convert to a pixel to handle
     auto* bitmapPixels = (int32_t *) data;
     auto* line = (uint32_t *) buffer.bits;

     for (int y = 0; y < buffer.height; y++) {
         for (int x = 0; x < buffer.width; x++) {
             line[x] = bitmapPixels[buffer.height * y + x];
         }
         line = line + buffer.stride;
     }

     //Unlock the drawing surface of the window
     if (ANativeWindow_unlockAndPost(window) < 0) {
         LOGI("java/lang/RuntimeException: unable to unlock and post to native window");
     }

     //free the reference to the Surface
     ANativeWindow_release(window);
 }*/


/*
* native_window_set_crop(..., crop)
* Sets which region of the next queued buffers needs to be considered.
* Depending on the scaling mode, a buffer's crop region is scaled and/or
* cropped to match the surface's size.  This function sets the crop in
* pre-transformed buffer pixel coordinates.
*
* The specified crop region applies to all buffers queued after it is called.
*
* If 'crop' is NULL, subsequently queued buffers won't be cropped.
*
* An error is returned if for instance the crop region is invalid, out of the
* buffer's bound or if the window is invalid.
*/
/*
int native_window_set_crop(struct ANativeWindow* window, android_native_rect_t const* crop)
{
    return window->perform(window, NATIVE_WINDOW_SET_CROP, crop);
}*/

int first = 1;

//draw a frame
static void engine_draw_frame(struct engine* engine) {
    /*
    if (engine->display == nullptr) {
        LOGI("engine->display is NULL");

        //No display.
        return;
    }

    //Just fill the screen with a color.
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


    eglSwapBuffers(engine->display, engine->surface);*/


    //make sure we have a window
    if (!engine_have_a_window(engine))
    {
        LOGI("The engine doesn't have a window !?\n");

        //abort
        goto draw_frame_end;
    }


    //ANativeWindow_Buffer in which to store the current frame's bits buffer
    ANativeWindow_Buffer buffer;

#ifdef OG
    //make sure we can lock this ANativeWindow_Buffer so that we can edit pixels
    if (ANativeWindow_lock(engine->app->window, &buffer, nullptr) < 0)
    {
        LOGI("Could not lock the window... :C\n");

        //abort
        goto draw_frame_end;
    }

    //fill the raw bits buffer
    fill_pixels(&buffer);

    //release the lock on our window's buffer and post the window to the screen
    ANativeWindow_unlockAndPost(engine->app->window);


#else
    if (dir == 0) {
        //adjust crop
        test_rect.left+=30;
        test_rect.right+=30;
        if (test_rect.left==900) {
            dir = 1;
        }
    }

    else {
        test_rect.left-=30;
        test_rect.right-=30;
        if (test_rect.left==0) {
            dir = 0;
        }
    }


    if (first == 1) {
        if (ANativeWindow_lock(engine->app->window, &buffer, nullptr) < 0)
        {
            LOGI("Could not lock the window... :C\n");

            //abort
            goto draw_frame_end;
        }
    }
    else {
        //lock the same buffer
        engine->app->window->perform(engine->app->window, 49, &buffer, nullptr);
    }

    //adjust the crop of the GraphicBuffer and push it to the screen
    LOGI("Calling set crop\n");
    engine->app->window->perform(engine->app->window, 3, &test_rect);

    //code 48 is a custom function we created in Surface.cpp
    engine->app->window->perform(engine->app->window, 48, false);
#endif

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
}*/

//process the next input event
static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
    auto* const engine = (struct engine*) app->userData;
    int32_t const current_event_type = AInputEvent_getType(event);

    //if the incoming event is a MotionEvent
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        //set animating to true

        //IGNORE TOUCH EVENTS FOR NOW

        engine->animating = 1;

        engine->state.x = AMotionEvent_getX(event, 0);

        engine->state.y = AMotionEvent_getY(event, 0);

        return 1;
    }

    //if the incoming event is a KeyEvent
    else if (current_event_type == AINPUT_EVENT_TYPE_KEY) {
        LOGI("Key event: action=%d keyCode=%d metaState=0x%x", AKeyEvent_getAction(event), AKeyEvent_getKeyCode(event), AKeyEvent_getMetaState(event));
    }
    return 0;
}

/*REFERENCE
 enum {
    // clang-format off
    NATIVE_WINDOW_SET_USAGE                       =  ANATIVEWINDOW_PERFORM_SET_USAGE
    NATIVE_WINDOW_CONNECT                         =  1,
    NATIVE_WINDOW_DISCONNECT                      =  2,
    NATIVE_WINDOW_SET_CROP                        =  3,
    NATIVE_WINDOW_SET_BUFFER_COUNT                =  4,
    NATIVE_WINDOW_SET_BUFFERS_GEOMETRY            =  ANATIVEWINDOW_PERFORM_SET_BUFFERS_GEOMETRY,
    NATIVE_WINDOW_SET_BUFFERS_TRANSFORM           =  6,
    NATIVE_WINDOW_SET_BUFFERS_TIMESTAMP           =  7,
    NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS          =  8,
    NATIVE_WINDOW_SET_BUFFERS_FORMAT              =  ANATIVEWINDOW_PERFORM_SET_BUFFERS_FORMAT,
    NATIVE_WINDOW_SET_SCALING_MODE                = 10,
    NATIVE_WINDOW_LOCK                            = 11,
    NATIVE_WINDOW_UNLOCK_AND_POST                 = 12,
    NATIVE_WINDOW_API_CONNECT                     = 13,
    NATIVE_WINDOW_API_DISCONNECT                  = 14,
    NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS     = 15,
    NATIVE_WINDOW_SET_POST_TRANSFORM_CROP         = 16,
    NATIVE_WINDOW_SET_BUFFERS_STICKY_TRANSFORM    = 17,
    NATIVE_WINDOW_SET_SIDEBAND_STREAM             = 18,
    NATIVE_WINDOW_SET_BUFFERS_DATASPACE           = 19,
    NATIVE_WINDOW_SET_SURFACE_DAMAGE              = 20,
    NATIVE_WINDOW_SET_SHARED_BUFFER_MODE          = 21,
    NATIVE_WINDOW_SET_AUTO_REFRESH                = 22,
    NATIVE_WINDOW_GET_REFRESH_CYCLE_DURATION      = 23,
    NATIVE_WINDOW_GET_NEXT_FRAME_ID               = 24,
    NATIVE_WINDOW_ENABLE_FRAME_TIMESTAMPS         = 25,
    NATIVE_WINDOW_GET_COMPOSITOR_TIMING           = 26,
    NATIVE_WINDOW_GET_FRAME_TIMESTAMPS            = 27,
    NATIVE_WINDOW_GET_WIDE_COLOR_SUPPORT          = 28,
    NATIVE_WINDOW_GET_HDR_SUPPORT                 = 29,
    NATIVE_WINDOW_SET_USAGE64                     = ANATIVEWINDOW_PERFORM_SET_USAGE64,
    NATIVE_WINDOW_GET_CONSUMER_USAGE64            = 31,
    NATIVE_WINDOW_SET_BUFFERS_SMPTE2086_METADATA  = 32,
    NATIVE_WINDOW_SET_BUFFERS_CTA861_3_METADATA   = 33,
    NATIVE_WINDOW_SET_BUFFERS_HDR10_PLUS_METADATA = 34,
    NATIVE_WINDOW_SET_AUTO_PREROTATION            = 35,
    NATIVE_WINDOW_GET_LAST_DEQUEUE_START          = 36,
    NATIVE_WINDOW_SET_DEQUEUE_TIMEOUT             = 37,
    NATIVE_WINDOW_GET_LAST_DEQUEUE_DURATION       = 38,
    NATIVE_WINDOW_GET_LAST_QUEUE_DURATION         = 39,
    NATIVE_WINDOW_SET_FRAME_RATE                  = 40,
    NATIVE_WINDOW_SET_CANCEL_INTERCEPTOR          = 41,
    NATIVE_WINDOW_SET_DEQUEUE_INTERCEPTOR         = 42,
    NATIVE_WINDOW_SET_PERFORM_INTERCEPTOR         = 43,
    NATIVE_WINDOW_SET_QUEUE_INTERCEPTOR           = 44,
    NATIVE_WINDOW_ALLOCATE_BUFFERS                = 45,
    NATIVE_WINDOW_GET_LAST_QUEUED_BUFFER          = 46,
    NATIVE_WINDOW_SET_QUERY_INTERCEPTOR           = 47,
    // clang-format on
};*/

/*FOR REFERENCE
* parameter for NATIVE_WINDOW_SET_SCALING_MODE
 * keep in sync with Surface.java in frameworks/base
enum {
    //the window content is not updated (frozen) until a buffer of
    //the window size is received (enqueued)
    NATIVE_WINDOW_SCALING_MODE_FREEZE           = 0,

    //the buffer is scaled in both dimensions to match the window size
    NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW  = 1,

    //the buffer is scaled uniformly such that the smaller dimension
    //of the buffer matches the window size (cropping in the process)
    NATIVE_WINDOW_SCALING_MODE_SCALE_CROP       = 2,

    //the window is clipped to the size of the buffer's crop rectangle; pixels
    //outside the crop rectangle are treated as if they are completely
    //transparent.
    NATIVE_WINDOW_SCALING_MODE_NO_SCALE_CROP    = 3,
};
 */

//process the next main command
static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
    //get our custom state engine that's attached to the passed android_app
    auto * __restrict const engine = (struct engine *) app->userData;

    switch (cmd) {
        case APP_CMD_SAVE_STATE:
            // The system has asked us to save our current state.  Do so.
            engine->app->savedState = malloc(sizeof(struct saved_state));
            *((struct saved_state*)engine->app->savedState) = engine->state;
            engine->app->savedStateSize = sizeof(struct saved_state);
            break;


        //if initialize window has been called for. The window is being shown, get it ready.
        case APP_CMD_INIT_WINDOW:
            LOGI("CMD_INIT_WINDOW found");

            //make sure engine->app->window is non-null
            if (engine_have_a_window(engine))
            {
                //ORIGINAL
                /*
                engine_init_display(engine);
                engine->renderer = new Renderer(1080, 2280);
                LOGI("New REnderer created");
                engine_draw_frame(engine);*/

#ifndef OG
                LOGI("INIT_WINDOW: Getting initial format");
                engine->initial_window_format = ANativeWindow_getFormat(app->window);


                LOGI("INIT_WINDOW: Setting buffer user dimensions to 2x");
                //set buffer user dimensions (trickles down to initial gralloc allocation request)
                engine->app->window->perform(engine->app->window, 15, 2280 * 2, 1080 * 2); //2160, 4560
#else


                //This one probably shouldn't be used, because it sets users dimensions, format, and scaling mode all in one
                //LOGI("INIT_WINDOW: setBuffersGeomoetry");
                ANativeWindow_setBuffersGeometry(app->window, ANativeWindow_getWidth(app->window) , ANativeWindow_getHeight(app->window) ,WINDOW_FORMAT_RGB_565);

#endif


#ifndef OG
                LOGI("INIT_WINDOW: Setting scaling mode...");
                //set scaling mode to crop no scale
                engine->app->window->perform(engine->app->window, 10, 3);

                LOGI("INIT_WINDOW: Setting color format");
                //set color format
                engine->app->window->perform(engine->app->window, 9, WINDOW_FORMAT_RGB_565);

                ANativeWindow_Buffer buffer;

                LOGI("INIT_WINDOW: calling ANativeWindow_lock()");


                //make sure we can lock this ANativeWindow_Buffer so that we can edit pixels
                if (ANativeWindow_lock(engine->app->window, &buffer, nullptr) < 0)
                {
                    LOGI("Could not lock the window... :C\n");
                    break;
                }

                //draw text once

                auto* current_pixel = (window_pixel_t *)buffer.bits;

                //draw white background
                for (int i = 0; i < 1080 * 2280 * 4; i++) {
                    current_pixel[i] = pixel_colors[4];
                }

                //draw actual text from the json coords
                drawText(200, 425, pixel_colors[2], current_pixel, buffer.stride);

                //adjust the crop of the GraphicBuffer
                LOGI("Calling set crop to crop size of screen\n");
                engine->app->window->perform(engine->app->window, 3, &test_rect);

                //LOGI("INIT_WINDOW: calling ANativeWindow_unlockAndPost()");
                //release the lock on our window's buffer and post the window to the screen
                //ANativeWindow_unlockAndPost(engine->app->window);

                engine->app->window->perform(engine->app->window, 48, false);
#endif

                //LOGI("engine_draw_frame for init_window");
                engine_draw_frame(engine);
                first = 0;
            }
            break;

        //window shutdown has been called for. The window is being hidden or closed, clean it up.
        case APP_CMD_TERM_WINDOW:
            //ORIGINAL
            /*if (engine->renderer) {
                auto* pRenderer = reinterpret_cast<Renderer*>(engine->renderer);
                engine->renderer = nullptr;
                delete pRenderer;
            }
            engine_term_display(engine);*/


            engine_term_display(engine);

            ANativeWindow_setBuffersGeometry(app->window, ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window), engine->initial_window_format);

            break;

        //When the app comes into focus (I think this is like onResume())
        case APP_CMD_GAINED_FOCUS:
            //When our app gains focus, we start monitoring the accelerometer.
            if (engine->accelerometerSensor != nullptr) {
                //Enable the accelerometer sensor
                ASensorEventQueue_enableSensor(engine->sensorEventQueue, engine->accelerometerSensor);


                // We'd like to get 60 events per second (in us).
                ASensorEventQueue_setEventRate(engine->sensorEventQueue, engine->accelerometerSensor,(1000L/60)*1000);
            }
            break;

        //When our app loses focus, we stop monitoring the accelerometer.
        //This is to avoid consuming battery while app is not being used.
        case APP_CMD_LOST_FOCUS:
            //disable accelerometer
            if (engine->accelerometerSensor != nullptr) {
                ASensorEventQueue_disableSensor(engine->sensorEventQueue, engine->accelerometerSensor);
            }


            //Stop animating
            engine->animating = 0;
            engine_draw_frame(engine);
            break;
        //"else"
        default:
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
    //make sure the passed ptr to android_app is non-null
    if (!app)
        return nullptr;


    typedef ASensorManager* (*PF_GETINSTANCEFORPACKAGE)(const char *name);
    void* androidHandle = dlopen("libandroid.so", RTLD_NOW);
    auto getInstanceForPackageFunc = (PF_GETINSTANCEFORPACKAGE) dlsym(androidHandle, "ASensorManager_getInstanceForPackage");


    if (getInstanceForPackageFunc) {
        JNIEnv* env = nullptr;
        app->activity->vm->AttachCurrentThread(&env, nullptr);

        jclass android_content_Context = env->GetObjectClass(app->activity->clazz);
        jmethodID midGetPackageName = env->GetMethodID(android_content_Context,"getPackageName","()Ljava/lang/String;");


        auto packageName = (jstring)env->CallObjectMethod(app->activity->clazz, midGetPackageName);

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
    auto getInstanceFunc = (PF_GETINSTANCE) dlsym(androidHandle, "ASensorManager_getInstance");


    //By all means at this point, ASensorManager_getInstance should be available
    assert(getInstanceFunc);
    dlclose(androidHandle);

    return getInstanceFunc();
}

//************************MAIN FXN**************************************************************************************************************************
/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 *
 * The android_native_app_glue library calls this fxn, passing it a predefined state structure. It also serves as a wrapper that simplifies handling of NativeActivity callbacks.
 */
void android_main(struct android_app* state) {
    //allocate memory on the heap for a window_pixel_t buffer 4x the size of screen
    large_buff = (window_pixel_t *)malloc(sizeof(window_pixel_t) * 2280 * 1080 * 4);

    get_txt_coords();

    //Next, the program handles events queued by the glue library. The event handler follows the state structure.

    //initialize a blank engine struct
    struct engine engine = {nullptr};

    //application can place a pointer to its own state object in userData (userData is just a void*)
    state->userData = &engine;

    //set callback function for activity lifecycle events (e.g. "pause", "resume")
    state->onAppCmd = engine_handle_cmd;

    //set callback fxn for input events coming from the AInputQueue attached to the activity
    state->onInputEvent = engine_handle_input;

    //assign our engine's "struct android_app" instance as the one that was passed into this function
    engine.app = state;

    //prep to monitor accelerometer

    //get an instance of SensorManager and assign it to the engine
    engine.sensorManager = AcquireASensorManagerInstance(state);

    //get the accelerometer and assign it to the engine
    engine.accelerometerSensor = ASensorManager_getDefaultSensor(engine.sensorManager,ASENSOR_TYPE_ACCELEROMETER);

    //create a sensor EventQueue using our app's ALooper and the SensorManager we got
    engine.sensorEventQueue = ASensorManager_createEventQueue(engine.sensorManager, state->looper, LOOPER_ID_USER,nullptr, nullptr);

    //possibly restore engine.state from a previously saved state
    if (state->savedState != nullptr) {
        //We are starting with a previous saved state; restore from it.
        engine.state = *(struct saved_state*)state->savedState;
    }


    //Next, a loop begins, in which application polls system for messages (sensor events). It sends messages to android_native_app_glue, which checks to see whether they
    //match any onAppCmd events defined in android_main. When a match occurs, the message is sent to the handler for execution
    //int result = system("sh /system/bin/stop vendor.hwcomposer-2-4");

    //LOGI("Result of call is %d", result);

    //loop waiting for stuff to do.
    while (true) {
        //Read all pending events.
        int ident;
        int events;


        //this will be filled by ALooper_pollAll
        struct android_poll_source* source;


        /*ALooper_pollOnce() fxn

        Waits for events to be available, with optional timeout in milliseconds.
        Invokes callbacks for all file descriptors on which an event occurred.
        If the timeout is zero, returns immediately without blocking. If the timeout is negative, waits indefinitely until an event appears.
        Returns ALOOPER_POLL_WAKE if the poll was awoken using wake() before the timeout expired and no callbacks were invoked and no other file descriptors were ready.
        Returns ALOOPER_POLL_CALLBACK if one or more callbacks were invoked.
        Returns ALOOPER_POLL_TIMEOUT if there was no data before the given timeout expired.
        Returns ALOOPER_POLL_ERROR if an error occurred.
        Returns a value >= 0 containing an identifier (the same identifier ident passed to ALooper_addFd()) if its file descriptor has data and
         it has no callback function (requiring the caller here to handle it).
         In this (and only this) case, outFd, outEvents and outData will contain the poll events and data associated with the fd, otherwise they will be set to NULL.
        This method does not return until it has finished invoking the appropriate callbacks for all file descriptors that were signalled.
        */

        //ALooper_pollAll() is like ALooper_pollOnce(), but performs all pending callbacks until all data has been consumed or a file descriptor is available with no callback.
        //It will never return ALOOPER_POLL_CALLBACK


        //If not animating, we will block forever waiting for events.
        //If animating, we loop until all events are read, then continue to draw the next frame of animation.
        while ((ident = ALooper_pollAll(engine.animating ? 0 : -1, nullptr, &events, (void **) &source)) >= 0) //outFd, outEvents, outData
        {
            //A return value >=0 means we need to handle the callback. outFd, outEvents, and outData now contain the poll events and data associated with the file descriptor

            //Process this event, which will call appropriate engine_handle_ fxn
            if (source != nullptr) {
                source->process(state, source);
            }


            //If a sensor has data, process it now.
            if (ident == LOOPER_ID_USER) {
                //make sure we've assigned the engine an accelerometerSensor and it's non-null
                if (engine.accelerometerSensor != nullptr) {
                    //new ASensorEvent
                    ASensorEvent event;

                    //get one single ASensorEvent off the queue
                    while (ASensorEventQueue_getEvents(engine.sensorEventQueue, &event, 1) > 0) {
                        //log the accelerometer data
                        //LOGI("accelerometer: x=%f y=%f z=%f", event.acceleration.x, event.acceleration.y, event.acceleration.z);
                    }
                }
            }


            //Check if we are exiting
            if (state->destroyRequested != 0) {
                LOGI("Engine thread destroy requested!");

                //terminate the display and return
                engine_term_display(&engine);
                return;
            }
        }


        //Once queue is empty, and the program exits the polling loop, the program calls OpenGL (or in our case uses ANativeWindow) to draw the screen

        //see if we're animating, and draw the next frame if so
        if (engine.animating) {
            //Done with events; draw next animation frame
            engine.state.angle += .01f;

            if (engine.state.angle > 1) {
                engine.state.angle = 0;
            }

            // Drawing is throttled to the screen update rate, so there is no need to do timing here.
            //LOGI("Engine animating");
            engine_draw_frame(&engine);
        }
    }
}


/*A little about Looper, Handler, Messages/MessageQueue...
 *
 * Looper can be attached to a background thread to manage a MessageQueue and constantly read the queue executing the queued work.
 * Only one Looper and one MessageQueue per thread.
 * A Handler is part of a thread and associated with the thread's looper. There can be multiple Handlers per thread.
 * The Handler can post runnables (using post() method) to be queued and run by the Looper
 * The Handler's handleMessages() callback fxn can be configured so that Messages with data can be sent to the background thread from outside that thread using the Handler's sendMessage() fxn.
 *
 * */

//********************************************************END MAIN FXN**********************************************************

//END_INCLUDE(all)
