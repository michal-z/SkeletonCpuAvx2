#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <immintrin.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


#define k_DemoName "Skeleton (CPU, AVX2)"
#define k_DemoResolutionX 1280
#define k_DemoResolutionY 720
#define k_DemoRcpResolutionX (1.0f / k_DemoResolutionX)
#define k_DemoRcpResolutionY (1.0f / k_DemoResolutionY)
#define k_DemoAspectRatio ((float)k_DemoResolutionX / k_DemoResolutionY)

#define k_TileSize 16
#define k_TileCountX (k_DemoResolutionX / k_TileSize)
#define k_TileCountY (k_DemoResolutionY / k_TileSize)
#define k_TileCount (k_TileCountX * k_TileCountY)

#define k_ThreadMaxCount 32

typedef struct __declspec(align(64)) WorkerThread
{
    uint8_t *displayPtr;
    HANDLE handle;
    HANDLE beginEvent;
    HANDLE endEvent;
} WorkerThread;

typedef struct Demo
{
    uint8_t *displayPtr;
    HWND window;
    HDC windowDevCtx;
    HDC memoryDevCtx;
    uint32_t threadCount;
    WorkerThread threads[k_ThreadMaxCount];
} Demo;

__declspec(align(64)) static uint32_t s_TileIndex[16];

static double
GetTime(void)
{
    static LARGE_INTEGER startCounter;
    static LARGE_INTEGER frequency;
    if (startCounter.QuadPart == 0)
    {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&startCounter);
    }
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (counter.QuadPart - startCounter.QuadPart) / (double)frequency.QuadPart;
}

static void
UpdateFrameTime(HWND window, double *time, float *deltaTime)
{
    static double lastTime = -1.0;
    static double lastFpsTime = 0.0;
    static unsigned frameCount = 0;

    if (lastTime < 0.0)
    {
        lastTime = GetTime();
        lastFpsTime = lastTime;
    }

    *time = GetTime();
    *deltaTime = (float)(*time - lastTime);
    lastTime = *time;

    if ((*time - lastFpsTime) >= 1.0)
    {
        const double fps = frameCount / (*time - lastFpsTime);
        const double ms = (1.0 / fps) * 1000.0;
        char text[256];
        snprintf(text, sizeof(text), "[%.1f fps  %.3f ms] %s", fps, ms, k_DemoName);
        SetWindowText(window, text);
        lastFpsTime = *time;
        frameCount = 0;
    }
    frameCount++;
}

static LRESULT CALLBACK
ProcessWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
        {
            PostQuitMessage(0);
            return 0;
        }
        break;
    }
    return DefWindowProc(window, message, wparam, lparam);
}

static void
InitializeWindow(Demo *demo)
{
    WNDCLASS winclass = {
        .lpfnWndProc = ProcessWindowMessage,
        .hInstance = GetModuleHandle(NULL),
        .hCursor = LoadCursor(NULL, IDC_ARROW),
        .lpszClassName = k_DemoName
    };
    if (!RegisterClass(&winclass))
        assert(0);

    RECT rect = { 0, 0, k_DemoResolutionX, k_DemoResolutionY };
    if (!AdjustWindowRect(&rect, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX, 0))
        assert(0);

    demo->window = CreateWindowEx(
        0, k_DemoName, k_DemoName, WS_OVERLAPPED | WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, NULL, 0);
    assert(demo->window);

    demo->windowDevCtx = GetDC(demo->window);
    assert(demo->windowDevCtx);

    BITMAPINFO bi = {
        .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
        .bmiHeader.biPlanes = 1,
        .bmiHeader.biBitCount = 32,
        .bmiHeader.biCompression = BI_RGB,
        .bmiHeader.biWidth = k_DemoResolutionX,
        .bmiHeader.biHeight = k_DemoResolutionY,
        .bmiHeader.biSizeImage = k_DemoResolutionX * k_DemoResolutionY
    };
    HBITMAP hbm = CreateDIBSection(demo->windowDevCtx, &bi, DIB_RGB_COLORS, (void **)&demo->displayPtr, NULL, 0);
    assert(hbm);

    demo->memoryDevCtx = CreateCompatibleDC(demo->windowDevCtx);
    assert(demo->memoryDevCtx);

    SelectObject(demo->memoryDevCtx, hbm);
}

static DWORD WINAPI
DrawTilesThread(void *param)
{
    WorkerThread *thread = (WorkerThread *)param;

    for (;;)
    {
        WaitForSingleObject(thread->beginEvent, INFINITE);

        //DrawTiles(thread.displayPtr, thread.zoom, thread.position[0], thread.position[1]);

        SetEvent(thread->endEvent);
    }
}

static void
InitializeWorkerThreads(Demo *demo)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    demo->threadCount = (uint32_t)(si.dwNumberOfProcessors - 1);

    for (uint32_t i = 0; i < demo->threadCount; ++i)
    {
        demo->threads[i].displayPtr = demo->displayPtr;
        demo->threads[i].handle = NULL;
        demo->threads[i].beginEvent = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);
        demo->threads[i].endEvent = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);

        assert(demo->threads[i].beginEvent);
        assert(demo->threads[i].endEvent);
    }

    for (uint32_t i = 0; i < demo->threadCount; ++i)
    {
        demo->threads[i].handle = CreateThread(NULL, 0, DrawTilesThread, (void *)&demo->threads[i], 0, NULL);
        assert(demo->threads[i].handle);
    }
}

int
main(void)
{
    SetProcessDPIAware();

    Demo demo = { 0 };
    InitializeWindow(&demo);

    for (;;)
    {
        MSG msg = { 0 };
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                break;
        }
        else
        {
            double time;
            float deltaTime;
            UpdateFrameTime(demo.window, &time, &deltaTime);

            BitBlt(demo.windowDevCtx, 0, 0, k_DemoResolutionX, k_DemoResolutionY, demo.memoryDevCtx, 0, 0, SRCCOPY);
        }
    }

    return 0;
}
// vim: ts=4 sw=4 expandtab:
