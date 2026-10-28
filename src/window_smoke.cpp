// SPDX-License-Identifier: MIT
// Bounded Win32/NoGraphicsAPI presentation correctness experiment. No SDK,
// visible window, shader, device-loss simulation or image-quality claim.
#if !defined(_WIN32)
#error The Basix window smoke target requires Win32; use the headless GPU/CPU targets elsewhere.
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
struct InjectedFailure : std::runtime_error { using std::runtime_error::runtime_error; };
struct ExtentRecord { unsigned width,height; };
struct Results {
    unsigned presented=0,empty_acquires=0,size_messages=0;
    double max_zero_acquire_ms=0;
    bool validation_enabled=false,window_destroyed=false,window_remained_hidden=true;
    std::vector<ExtentRecord> extents;
};

class Window {
public:
    HWND handle=nullptr;
    explicit Window(Results& results):results_(results) {
        name_=L"Basix_Hidden_WindowSmoke_"+std::to_wstring(GetCurrentProcessId());
        WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.lpfnWndProc=procedure;
        wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=name_.c_str();
        require(RegisterClassExW(&wc)!=0,"RegisterClassExW failed");
        registered_=true;
        handle=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,name_.c_str(),L"Basix hidden swapchain smoke",
                              WS_POPUP,-32000,-32000,64,64,nullptr,nullptr,wc.hInstance,&results_);
        if(!handle) { UnregisterClassW(name_.c_str(),wc.hInstance);registered_=false;throw std::runtime_error("CreateWindowExW failed"); }
        ShowWindow(handle,SW_HIDE);
        require(!IsWindowVisible(handle),"Smoke window must never be visible");
    }
    Window(const Window&)=delete;
    Window& operator=(const Window&)=delete;
    ~Window() {
        if(handle) { const HWND old=handle;DestroyWindow(handle);handle=nullptr;results_.window_destroyed=!IsWindow(old); }
        if(registered_)UnregisterClassW(name_.c_str(),GetModuleHandleW(nullptr));
        // Drain the WM_QUIT produced by this owned test window only.
        MSG message{};while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){}
    }
    void pump() {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            require(message.message!=WM_QUIT,"Unexpected close during window test");
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        results_.window_remained_hidden=results_.window_remained_hidden&&!IsWindowVisible(handle);
        require(results_.window_remained_hidden,"Smoke window became visible");
    }
    void resize(unsigned width,unsigned height) {
        require(SetWindowPos(handle,nullptr,0,0,static_cast<int>(width),static_cast<int>(height),
                             SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_HIDEWINDOW)!=0,"Hidden window resize failed");
        pump();RECT client{};require(GetClientRect(handle,&client)!=0,"GetClientRect failed");
        require(client.right-client.left==static_cast<LONG>(width)&&client.bottom-client.top==static_cast<LONG>(height),"Win32 client extent mismatch");
    }
private:
    Results& results_;std::wstring name_;bool registered_=false;
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam) noexcept {
        if(message==WM_NCCREATE) {
            const auto* create=reinterpret_cast<const CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* results=reinterpret_cast<Results*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        switch(message) {
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:if(results)++results->size_messages;return 0;
        case WM_CLOSE:ShowWindow(hwnd,SW_HIDE);return 0;
        case WM_DESTROY:PostQuitMessage(0);return 0;
        default:return DefWindowProcW(hwnd,message,wparam,lparam);
        }
    }
};

class Session {
public:
    gpu::Device* device=nullptr;
    explicit Session(HWND window,bool validation,Results& result) {
#if defined(NDEBUG)
        require(!validation,"--validate requires a Debug build");
#endif
        gpu::set_error_callback([](const char* text,bool) noexcept { std::cerr<<"Basix window backend: "<<text<<'\n'; });
        // The pinned backend may request STORAGE usage for swapchain images.
        // UNORM supports that on the tested adapter; sRGB does not. This clear-only
        // experiment does not establish a production HDR/sRGB presentation path.
        const auto initialized=gpu::create_device({.window=window,.swapchain_format=gpu::Format::bgra8_unorm});
        device=initialized.device;
        if(!device)throw std::runtime_error("Windowed NoGraphicsAPI device creation failed: "+std::to_string(static_cast<int>(initialized.error)));
        try {
            result.validation_enabled=gpu::basix_native_vulkan(device).validation_enabled;
            require(!validation||result.validation_enabled,"Khronos validation was required but not enabled");
            pool_=gpu::create_command_pool(device);
            timeline_=gpu::create_timeline_semaphore(device);
            require(pool_&&timeline_,"Window command resources unavailable");
        } catch(...) { close();throw; }
    }
    ~Session(){close();}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    void close() noexcept {
        if(!device)return;
        gpu::wait_idle(device);
        if(pool_)gpu::destroy_command_pool(pool_);
        if(timeline_)gpu::destroy_timeline_semaphore(timeline_);
        gpu::destroy_device(device);device=nullptr;pool_=nullptr;timeline_=nullptr;
    }
    void frame(unsigned width,unsigned height,Results& result) {
        const auto drawable=gpu::get_drawable_extent(device);
        require(drawable.x==width&&drawable.y==height,"Vulkan drawable extent differs from actual Win32 client extent");
        gpu::reset_command_pool(pool_);
        auto* commands=gpu::begin_commands(pool_);require(commands!=nullptr,"begin_commands returned null");
        const auto started=std::chrono::steady_clock::now();
        const auto frame=gpu::acquire(commands);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(width==0||height==0) {
            require(frame.render_view==nullptr&&frame.extent.x==0&&frame.extent.y==0,"Zero-size acquire must return an empty frame");
            require(elapsed<1000,"Zero-size acquire did not return promptly");
            result.max_zero_acquire_ms=std::max(result.max_zero_acquire_ms,elapsed);++result.empty_acquires;
            // Empty acquire records no presentation work. End and discard this
            // unsubmitted command buffer; the previous submitted frame is complete.
            gpu::end_commands(commands);return;
        }
        require(frame.render_view&&frame.extent.x==width&&frame.extent.y==height,"Acquired swapchain frame extent mismatch");
        gpu::begin_render_pass(commands,{.colors={{.render_view=frame.render_view,.load=gpu::LoadOp::clear,
                                                   .clear={.x=0.03f,.y=0.14f,.z=0.2f,.w=1.0f}}}});
        gpu::end_render_pass(commands);gpu::end_commands(commands);
        const gpu::TimelinePoint done{.semaphore=timeline_,.value=++serial_};
        gpu::submit_and_present(device,{.commands={commands},.completion=done});gpu::wait_timeline(done);++result.presented;
    }
private:
    gpu::CommandPool* pool_=nullptr;gpu::TimelineSemaphore* timeline_=nullptr;uint64_t serial_=0;
};

void write_result(const std::filesystem::path& output,const Results& r,bool injected) {
    std::ofstream file(output/"result.json");
    file<<"{\"status\":\""<<(injected?"expected_failure":"passed")<<"\",\"presented_frames\":"<<r.presented
        <<",\"empty_acquires\":"<<r.empty_acquires<<",\"max_zero_acquire_ms\":"<<r.max_zero_acquire_ms
        <<",\"validation_enabled\":"<<(r.validation_enabled?"true":"false")
        <<",\"window_destroyed\":"<<(r.window_destroyed?"true":"false")
        <<",\"window_remained_hidden\":"<<(r.window_remained_hidden?"true":"false")
        <<",\"size_messages\":"<<r.size_messages<<",\"extents\":[";
    for(size_t i=0;i<r.extents.size();++i) { if(i)file<<',';file<<'['<<r.extents[i].width<<','<<r.extents[i].height<<']'; }
    file<<"],\"scope\":\"Hidden Win32 swapchain acquire/clear/present, actual client resize to zero and restoration, same thread/queue. OS minimize, pixel readback, SDK evaluation and device loss not tested.\",\"validation_scope\":\"Layer enablement only; the runner checks whole-process stderr including teardown.\"}\n";
    require(bool(file),"Window smoke receipt write failed");
}
}

int main(int argc,char** argv) {
    Results result;bool expected_failure=false,validate=false;std::string fail_at;std::filesystem::path output="window-output";
    try {
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--validate")validate=true;
            else if(arg=="--output"&&i+1<argc)output=argv[++i];
            else if(arg=="--fail-at"&&i+1<argc)fail_at=argv[++i];
            else throw std::runtime_error("Usage: basix_window_smoke [--output DIRECTORY] [--validate] [--fail-at before-device|after-device|after-present]");
        }
        require(fail_at.empty()||fail_at=="before-device"||fail_at=="after-device"||fail_at=="after-present","Unknown injected failure phase");
        std::filesystem::create_directories(output);
        try {
            Window window(result);
            if(fail_at=="before-device")throw InjectedFailure("Controlled failure before device initialization");
            Session session(window.handle,validate,result);
            if(fail_at=="after-device")throw InjectedFailure("Controlled failure after device initialization");
            for(const auto extent:std::array<ExtentRecord,5>{{{64,64},{320,180},{17,9},{0,0},{96,54}}}) {
                gpu::wait_idle(session.device);window.resize(extent.width,extent.height);result.extents.push_back(extent);
                const unsigned frames=extent.width?2:3;
                for(unsigned i=0;i<frames;++i) { window.pump();session.frame(extent.width,extent.height,result); }
                if(fail_at=="after-present")throw InjectedFailure("Controlled failure after completed presentation");
            }
            require(result.presented==8&&result.empty_acquires==3,"Window test coverage incomplete");
            session.close();
        } catch(const InjectedFailure& e) { expected_failure=true;std::cout<<e.what()<<'\n'; }
        require(result.window_destroyed&&result.window_remained_hidden,"Owned test window did not cleanly close hidden");
        require(fail_at.empty()!=expected_failure,"Expected injected failure was not exercised");
        write_result(output,result,expected_failure);
        std::cout<<(expected_failure?"EXPECTED FAILURE / CLEANUP PASS":"PASS: hidden window resize/zero-size/restore and clean shutdown")<<'\n';
        return expected_failure?2:0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
