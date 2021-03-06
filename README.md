Right now it just displays a statically drawn cube bouncing back and forth. What do I mean by statically drawn? I malloc a buffer that’s 4x as large as the screen and draw the cube **once**. Then on each frame I index into the buffer at incremental memory locations so that the cube appears to move. (I change the area of the buffer that's reflected on the screen.)  

A little more about how it works: when the app launches, it triggers the callback function onNativeWindowCreated() in android_native_app_glue.c, which captures a handle to the ANativeWindow (C++ version of Android's Surface class) that the app is displaying. We can leverage that ANativeWindow's raw pixel buffer by calling ANativeWindow_lock(), which in turn calls Surface::lock() in Surface.cpp, which gives us a pointer to the raw memory containing the bits of the actual GraphicBuffer (a wrapper around a raw buffer allocated as needed by Gralloc3). Then we can do our software rendering and call ANativeWindow_unlockAndPost(), which queues the buffer back up to the BufferQueue, where it will await processing by SurfaceFlinger on the next VSYNC pulse from the display hardware.

Compare this to my DRM dumb buffer repository, which is doing lower-level rendering by interacting with the kernel and the device's DRM driver. That code shuts down SurfaceFlinger/hwcomposer and assumes the role of the display server.  

# TODO #
Currently, the main issue is that I’m still using memcpy to copy all of the pixel data from my malloced buffer into the window buffer (just adjusting the source address in the memcpy() call). I can set the window’s bitbuffer address to point to my malloced buffer, and it appears to change when I check it inside my program, but that’s futile--when the window is posted to the display queue (sent back into the Android graphics pipeline files like Surface.cpp), the system still looks for the pixels at the original address where the window’s bitbuffer was allocated. The reason for that is as follows: when ANativeWindow_lock(ANativeWindow\* window) is called, it ends up just getting the underlying Surface instance from `window`, which contains the original instance of GraphicBuffer that's attached to the Surface, which still holds the original bits address. The `buffer->bits` pointer that we get in main.cpp is really just a copy of the original pointer, as we can see here in Surface::lock():   

```
//a copy of the ptr to the raw bits of the window
void* vaddr;

//populate vaddr with pointer to the raw buffer bits by calling lockAsync() on our GraphicBuffer, reading bits address into vaddr. lockAsync() is just getting a copy of the original pointer held by the GraphicBuffer
status_t res = backBuffer->lockAsync(GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN, newDirtyRegion.bounds(), &vaddr, fenceFd);
```   

So I have no choice but to write pixels into that address in my program. I'm currently trying to, within my program, get access to that original bits address and change it to point to my allocated pixel memory. Then I'd be able to save more rendering time by just adjusting the pointer (which I **can** do in the DRM dumb buffer version of this app) and not having to use memcpy.

UPDATE(02/02/21): I drew some text into the ANativeWindow_Buffer. For a sample text, go to my text pixel coordinate generator at [this](http://serviceberry3.github.io/text_eng.html), download the json, and ```adb push``` it to /system/files/text_coords.json on the Android device. I will soon update the text engine page to take input for custom text.  

Example:  
![screenshot of noshake v2](img/noshake_v2.png)

UPDATE(01/07/21): I created a new version of the unlockAndPost() function in Surface.cpp (in AOSP source code) which avoids setting mLockedBuffer back to null after each unlock. So it reuses the same buffer. Then I also needed to comment out the isDequeued() check in BufferQueueProducer.cpp. Using Surface::perform() with SET_BUFFERS_USER_DIMENSIONS, I can force gralloc to create a buffer 4x as large as the screen, and then on each frame I just adjust the crop rectangle and unlockAndPost() the same buffer over and over again. I also created a custom version of Surface::lock() that doesn't call dequeueBufer(); it just reuses the last buffer and calls lockAsync() on that.

However, something interesting was happening after I made these changes: the app started rendering at the expected speed, but then the time it took for my custom unlockAndPost() function to run gradually increases and the rendering gradually slows down. 

After doing some deep diving, including running '''dumpsys SurfaceFlinger''' and using the Android systrace tool, I discovered that I was flooding the BufferQueue: queueBuffer() calls were running rampant. Normally, the queue/dequeue pairs are spaced out about as far apart as the VSYNCS, and "fence wait" happens in the lockAsync() function. "fence wait" ends in sync with "operation id," whatever that is...anyway, the issue is that in my hacked version, the queueBuffer() calls aren't timed correctly, because the lockAsync() function in Surface::lock() isn't waiting for a HWC release fence anymore. I'm trying to figure out why.




