package com.opengrace.app;

import org.libsdl.app.SDLActivity;

/**
 * The OpenGrace app shell (decision 52): a bare executable cannot initialise
 * SDL on Android, so the engine runs through SDLActivity. The Java side
 * declares the library order (SDL2 then main) and the fixed command line the
 * hardware run passes to the engine's main() through SDL_main.
 */
public class GraceActivity extends SDLActivity
{
    /**
     * The library order SDLActivity loads: the static SDL2 built with JNI
     * entry points, then the engine's libmain.so (the grace CMake target).
     */
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
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
