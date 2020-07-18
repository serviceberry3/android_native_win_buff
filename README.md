Right now it just displays a cool stripey Christmas screen!

A little about how it works: when the app launches, it triggers the callback function onNativeWindowCreated() in android_native_app_glue.c, which captures a handle to the ANativeWindow (C++ version of Android's Surface class) that the app is displaying. We can leverage that ANativeWindow's raw pixel buffer by calling ANativeWindow_lock(), which in turn calls Surface::lock() in Surface.cpp, which gives us a pointer to the raw memory containing the bits of the actual GraphicBuffer (a wrapper around a raw buffer allocated as needed by Gralloc3). Then we can do our software rendering and call ANativeWindow_unlockAndPost(), which queues the buffer back up to the BufferQueue, where it will await processing by SurfaceFlinger on the next VSYNC pulse from the display hardware.

Compare this to my DRM dumb buffer repository, which is doing lower-level rendering by interacting with the kernel and the device's DRM driver.
