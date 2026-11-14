package com.opengrace.app;

import org.libsdl.app.SDLActivity;

/**
 * The OpenGrace app shell (decision 52): a bare executable cannot initialise
 * SDL on Android, so the engine runs through SDLActivity. The Java side
 * declares the library list (the engine's libmain.so) and the fixed command
 * line the hardware run passes to the engine's main() through SDL_main.
 * hardware run passes to the engine's main() through SDL_main.
 */
public class GraceActivity extends SDLActivity
{
    /**
     * The library SDLActivity loads: the engine's libmain.so (the grace
     * CMake target). The SDL2 with the JNI entry points is linked
     * statically inside libmain.so, so there is no separate libSDL2.so.
     */
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /**
     * The fixed command line the hardware run uses: the Vulkan backend,
     * ten frames. The engine's main() is unchanged — the same sources
     * build the desktop executables and this shared library.
     */
    @Override
    protected String[] getArguments() {
        return new String[] { "--vulkan", "--frames", "10", "-v" };
    }
}
