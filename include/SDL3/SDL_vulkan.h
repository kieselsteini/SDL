/*
  Simple DirectMedia Layer
  Copyright (C) 2017, Mark Callow

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

/**
 * # CategoryVulkan
 *
 * Functions for creating Vulkan surfaces on SDL windows.
 */

#ifndef SDL_vulkan_h_
#define SDL_vulkan_h_

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#include <SDL3/SDL_begin_code.h>
/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/* Avoid including vulkan.h, don't define VkInstance if it's already included */
#ifdef VULKAN_H_
#define NO_SDL_VULKAN_TYPEDEFS
#endif
#ifndef NO_SDL_VULKAN_TYPEDEFS
#define VK_DEFINE_HANDLE(object) typedef struct object##_T* object;

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(_M_X64) || defined(__ia64) || defined (_M_IA64) || defined(__aarch64__) || defined(__powerpc64__)
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef struct object##_T *object;
#else
#define VK_DEFINE_NON_DISPATCHABLE_HANDLE(object) typedef uint64_t object;
#endif

VK_DEFINE_HANDLE(VkInstance)
VK_DEFINE_NON_DISPATCHABLE_HANDLE(VkSurfaceKHR)
struct VkAllocationCallbacks;

#endif /* !NO_SDL_VULKAN_TYPEDEFS */

/**
 *  \name Vulkan support functions
 */
/* @{ */

/**
 * Dynamically load the Vulkan loader library.
 *
 * This should be called after initializing the video driver, but before
 * creating any Vulkan windows. If no Vulkan loader library is loaded, the
 * default library will be loaded upon creation of the first Vulkan window.
 *
 * It is fairly common for Vulkan applications to link with libvulkan instead
 * of explicitly loading it at run time. This will work with SDL provided the
 * application links to a dynamic library and both it and SDL use the same
 * search path.
 *
 * If you specify a non-NULL `path`, an application should retrieve all of the
 * Vulkan functions it uses from the dynamic library using
 * SDL_Vulkan_GetVkGetInstanceProcAddr unless you can guarantee `path` points
 * to the same vulkan loader library the application linked to.
 *
 * On Apple devices, if `path` is NULL, SDL will attempt to find the
 * `vkGetInstanceProcAddr` address within all the Mach-O images of the current
 * process. This is because it is fairly common for Vulkan applications to
 * link with libvulkan (and historically MoltenVK was provided as a static
 * library). If it is not found, on macOS, SDL will attempt to load
 * `vulkan.framework/vulkan`, `libvulkan.1.dylib`,
 * `MoltenVK.framework/MoltenVK`, and `libMoltenVK.dylib`, in that order. On
 * iOS, SDL will attempt to load `libMoltenVK.dylib`. Applications using a
 * dynamic framework or .dylib must ensure it is included in its application
 * bundle.
 *
 * On non-Apple devices, application linking with a static libvulkan is not
 * supported. Either do not link to the Vulkan loader or link to a dynamic
 * library version.
 *
 * \param path The platform dependent Vulkan loader library name or NULL
 * \returns 0 on success or a negative error code on failure; call
 *          SDL_GetError() for more information.
 *
 * \since This function is available since SDL 3.0.0.
 *
 * \sa SDL_Vulkan_GetVkGetInstanceProcAddr
 * \sa SDL_Vulkan_UnloadLibrary
 */
extern DECLSPEC int SDLCALL SDL_Vulkan_LoadLibrary(const char *path);

/**
 * Get the address of the `vkGetInstanceProcAddr` function.
 *
 * This should be called after either calling SDL_Vulkan_LoadLibrary() or
 * creating an SDL_Window with the `SDL_WINDOW_VULKAN` flag.
 *
 * The actual type of the returned function pointer is
 * PFN_vkGetInstanceProcAddr, but that isn't available because the Vulkan
 * headers are not included here. You should cast the return value of this
 * function to that type, e.g.
 *
 * `vkGetInstanceProcAddr =
 * (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();`
 *
 * \returns the function pointer for `vkGetInstanceProcAddr` or NULL on error.
 *
 * \since This function is available since SDL 3.0.0.
 */
extern DECLSPEC SDL_FunctionPointer SDLCALL SDL_Vulkan_GetVkGetInstanceProcAddr(void);

/**
 * Unload the Vulkan library previously loaded by SDL_Vulkan_LoadLibrary().
 *
 * \since This function is available since SDL 3.0.0.
 *
 * \sa SDL_Vulkan_LoadLibrary
 */
extern DECLSPEC void SDLCALL SDL_Vulkan_UnloadLibrary(void);

/**
 * Get the Vulkan instance extensions needed for vkCreateInstance.
 *
 * This should be called after either calling SDL_Vulkan_LoadLibrary() or
 * creating an SDL_Window with the `SDL_WINDOW_VULKAN` flag.
 *
 * On return, the variable pointed to by `count` will be set to the number of
 * elements returned, suitable for using with
 * VkInstanceCreateInfo::enabledExtensionCount, and the returned array can be
 * used with VkInstanceCreateInfo::ppEnabledExtensionNames, for calling
 * Vulkan's vkCreateInstance API.
 *
 * You should not free the returned array; it is owned by SDL.
 *
 * \param count a pointer filled in with the number of extensions returned.
 * \returns An array of extension name strings on success, NULL on error.
 *
 * \since This function is available since SDL 3.0.0.
 *
 * \sa SDL_Vulkan_CreateSurface
 */
extern DECLSPEC char const* const* SDLCALL SDL_Vulkan_GetInstanceExtensions(Uint32 *count);

/**
 * Create a Vulkan rendering surface for a window.
 *
 * The `window` must have been created with the `SDL_WINDOW_VULKAN` flag and
 * `instance` must have been created with extensions returned by
 * SDL_Vulkan_GetInstanceExtensions() enabled.
 *
 * If `allocator` is NULL, Vulkan will use the system default allocator. This
 * argument is passed directly to Vulkan and isn't used by SDL itself.
 *
 * \param window The window to which to attach the Vulkan surface
 * \param instance The Vulkan instance handle
 * \param allocator A VkAllocationCallbacks struct, which lets the app set the
 *                  allocator that creates the surface. Can be NULL.
 * \param surface A pointer to a VkSurfaceKHR handle to output the newly
 *                created surface
 * \returns SDL_TRUE on success, SDL_FALSE on error.
 *
 * \since This function is available since SDL 3.0.0.
 *
 * \sa SDL_Vulkan_GetInstanceExtensions
 * \sa SDL_Vulkan_DestroySurface
 */
extern DECLSPEC SDL_bool SDLCALL SDL_Vulkan_CreateSurface(SDL_Window *window,
                                                          VkInstance instance,
                                                          const struct VkAllocationCallbacks *allocator,
                                                          VkSurfaceKHR* surface);

/**
 * Destroy the Vulkan rendering surface of a window.
 *
 * This should be called before SDL_DestroyWindow, if SDL_Vulkan_CreateSurface
 * was called after SDL_CreateWindow.
 *
 * The `instance` must have been created with extensions returned by
 * SDL_Vulkan_GetInstanceExtensions() enabled and `surface` must have been
 * created successfully by an SDL_Vulkan_CreateSurface() call.
 *
 * If `allocator` is NULL, Vulkan will use the system default allocator. This
 * argument is passed directly to Vulkan and isn't used by SDL itself.
 *
 * \param instance The Vulkan instance handle
 * \param surface VkSurfaceKHR handle to destroy
 * \param allocator A VkAllocationCallbacks struct, which lets the app set the
 *                  allocator that destroys the surface. Can be NULL.
 *
 * \since This function is available since SDL 3.0.0.
 *
 * \sa SDL_Vulkan_GetInstanceExtensions
 * \sa SDL_Vulkan_CreateSurface
 */
extern DECLSPEC void SDLCALL SDL_Vulkan_DestroySurface(VkInstance instance,
                                                       VkSurfaceKHR surface,
                                                       const struct VkAllocationCallbacks *allocator);

/* @} *//* Vulkan support functions */

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif
#include <SDL3/SDL_close_code.h>

#endif /* SDL_vulkan_h_ */
