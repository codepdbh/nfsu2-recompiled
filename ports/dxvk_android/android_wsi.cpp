#include <cstdint>
#include "../../android/app/src/main/cpp/android_resolution.h"
#ifdef __ANDROID__
#include <android/native_window.h>
#else
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#undef None
#undef Always
#undef Status
struct ANativeWindow {Display* display;Window window;uint32_t width,height;};
#endif
#include <mutex>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include "src/wsi/wsi_presenter.h"
#include "src/wsi/wsi_window.h"
#include "src/wsi/wsi_monitor.h"
#include "src/dxvk/dxvk_platform_exts.h"

namespace {
std::mutex windowMutex;
ANativeWindow* currentWindow=nullptr;
#ifdef __ANDROID__
uintptr_t windowGeneration=0x1000;
#endif
HWND currentHandle=nullptr;
uint32_t screenWidth=1280,screenHeight=720;
uint32_t renderWidth=0,renderHeight=0;
HMONITOR monitor(){return reinterpret_cast<HMONITOR>(uintptr_t{1});}
// The Vulkan driver DXVK talks to: the linked system loader, or an imported
// adrenotools driver chosen in the launcher (set before the device exists).
std::atomic<PFN_vkGetInstanceProcAddr> instanceProcAddr{nullptr};
}
extern "C" void dxvkAndroidSetInstanceProcAddr(PFN_vkGetInstanceProcAddr entry){instanceProcAddr=entry;}
extern "C" PFN_vkGetInstanceProcAddr dxvkAndroidGetInstanceProcAddr(){
    auto entry=instanceProcAddr.load();return entry?entry:vkGetInstanceProcAddr;
}
extern "C" void dxvkAndroidSetRenderSize(uint32_t width,uint32_t height){std::lock_guard<std::mutex> guard(windowMutex);renderWidth=width;renderHeight=height;}
extern "C" HWND dxvkAndroidGetWindowHandle(){std::lock_guard<std::mutex> guard(windowMutex);return currentHandle;}
extern "C" void dxvkAndroidSetWindow(ANativeWindow* window){
    std::lock_guard<std::mutex> guard(windowMutex);
    if(window==currentWindow)return;
#ifdef __ANDROID__
    if(window){ANativeWindow_acquire(window);screenWidth=ANativeWindow_getWidth(window);screenHeight=ANativeWindow_getHeight(window);}
    if(currentWindow)ANativeWindow_release(currentWindow);
#endif
    currentWindow=window;
#ifdef __ANDROID__
    currentHandle=window?reinterpret_cast<HWND>(++windowGeneration):nullptr;
#else
    currentHandle=window;
#endif
}
#ifndef __ANDROID__
extern "C" ANativeWindow* dxvkCreateTestWindow(){
    XInitThreads();auto display=XOpenDisplay(nullptr);if(!display)return nullptr;
    Window window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1280,720,0,0,0);
    XMapWindow(display,window);XSync(display,False);return new ANativeWindow{display,window,1280,720};
}
extern "C" void dxvkDestroyTestWindow(ANativeWindow* window){if(!window)return;XDestroyWindow(window->display,window->window);XCloseDisplay(window->display);delete window;}
#endif
namespace dxvk {
DxvkPlatformExts DxvkPlatformExts::s_instance;
std::string_view DxvkPlatformExts::getName(){return "Android WSI";}
DxvkNameSet DxvkPlatformExts::getInstanceExtensions(){DxvkNameSet names;names.add(VK_KHR_SURFACE_EXTENSION_NAME);
#ifdef __ANDROID__
names.add(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#else
names.add(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
return names;}
DxvkNameSet DxvkPlatformExts::getDeviceExtensions(uint32_t){return {};}
void DxvkPlatformExts::initInstanceExtensions(){}
void DxvkPlatformExts::initDeviceExtensions(const DxvkInstance*){}
}
namespace dxvk::wsi {
VkResult createSurface(HWND window,const Rc<vk::InstanceFn>& instance,VkSurfaceKHR* surface){
    std::lock_guard<std::mutex> guard(windowMutex);
    if(!currentWindow||window!=currentHandle)return VK_ERROR_SURFACE_LOST_KHR;
#ifdef __ANDROID__
    auto create=reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(dxvkAndroidGetInstanceProcAddr()(instance->instance(),"vkCreateAndroidSurfaceKHR"));
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};info.window=currentWindow;
    return create(instance->instance(),&info,nullptr,surface);
#else
    auto create=reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(vkGetInstanceProcAddr(instance->instance(),"vkCreateXlibSurfaceKHR"));
    if(!create)return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkXlibSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};info.dpy=currentWindow->display;info.window=currentWindow->window;
    return create(instance->instance(),&info,nullptr,surface);
#endif
}
HMONITOR getDefaultMonitor(){return monitor();}
HMONITOR enumMonitors(uint32_t index){return index?nullptr:monitor();}
bool getDisplayName(HMONITOR handle,WCHAR (&name)[32]){if(handle!=monitor())return false;const wchar_t display[]=L"\\\\.\\DISPLAY1";std::fill(std::begin(name),std::end(name),0);std::copy(std::begin(display),std::end(display),name);return true;}
bool getDesktopCoordinates(HMONITOR handle,RECT* rect){if(handle!=monitor()||!rect)return false;std::lock_guard<std::mutex> guard(windowMutex);*rect={0,0,static_cast<LONG>(screenWidth),static_cast<LONG>(screenHeight)};return true;}
bool getCurrentDisplayMode(HMONITOR handle,WsiMode* mode){if(handle!=monitor()||!mode)return false;std::lock_guard<std::mutex> guard(windowMutex);*mode={screenWidth,screenHeight,{60,1},32,false};return true;}
bool getDesktopDisplayMode(HMONITOR handle,WsiMode* mode){return getCurrentDisplayMode(handle,mode);}
bool getDisplayMode(HMONITOR handle,uint32_t index,WsiMode* mode){
    if(handle!=monitor()||!mode)return false;
    std::lock_guard<std::mutex> guard(windowMutex);
    if(!renderWidth||!renderHeight){
        static constexpr AndroidRenderMode pcModes[]={{640,480},{800,600},{1024,768},{1280,960},{1280,1024},{1600,1200}};
        if(index>6)return false;
        auto selected=index==6?AndroidRenderMode{screenWidth,screenHeight}:pcModes[index];
        *mode={selected.width,selected.height,{60,1},32,false};return true;
    }
    if(index>=6)return false;
    auto sizes=androidRenderModes(screenWidth,screenHeight,renderWidth?renderWidth:screenWidth,renderHeight?renderHeight:screenHeight);
    *mode={sizes[index].width,sizes[index].height,{60,1},32,false};return true;
}
void getWindowSize(HWND window,uint32_t* width,uint32_t* height){std::lock_guard<std::mutex> guard(windowMutex);bool valid=currentWindow&&window==currentHandle;if(width)*width=valid?screenWidth:0;if(height)*height=valid?screenHeight:0;}
void resizeWindow(HWND window,DxvkWindowState*,uint32_t width,uint32_t height){std::lock_guard<std::mutex> guard(windowMutex);
#ifdef __ANDROID__
if(currentWindow&&window==currentHandle)ANativeWindow_setBuffersGeometry(currentWindow,width,height,0);
#else
(void)window;(void)width;(void)height;
#endif
}
bool isWindow(HWND window){std::lock_guard<std::mutex> guard(windowMutex);return currentWindow&&window==currentHandle;}
HMONITOR getWindowMonitor(HWND window){return isWindow(window)?monitor():nullptr;}
bool setWindowMode(HMONITOR handle,HWND window,const WsiMode*,bool){return handle==monitor()&&isWindow(window);}
bool enterFullscreenMode(HMONITOR handle,HWND window,DxvkWindowState*,bool){return handle==monitor()&&isWindow(window);}
bool leaveFullscreenMode(HWND window,DxvkWindowState*){return isWindow(window);}
bool restoreDisplayMode(HMONITOR handle){return handle==monitor();}
}
