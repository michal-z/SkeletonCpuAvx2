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

typedef struct JobData
{
    uint8_t *displayPtr;
} JobData;

typedef struct Demo
{
    HWND window;
    HDC windowDc;
    HDC memoryDc;
    uint32_t cpuCoreCount;
    JobData job;
    PTP_WORK jobHandle;
} Demo;

__declspec(align(64)) static volatile uint32_t s_TileIndex[16];

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
    static uint32_t frameCount = 0;

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
        double fps = frameCount / (*time - lastFpsTime);
        double ms = (1.0 / fps) * 1000.0;
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

    demo->windowDc = GetDC(demo->window);
    assert(demo->windowDc);

    BITMAPINFO bi = {
        .bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
        .bmiHeader.biPlanes = 1,
        .bmiHeader.biBitCount = 32,
        .bmiHeader.biCompression = BI_RGB,
        .bmiHeader.biWidth = k_DemoResolutionX,
        .bmiHeader.biHeight = k_DemoResolutionY,
        .bmiHeader.biSizeImage = k_DemoResolutionX * k_DemoResolutionY
    };
    HBITMAP hbm = CreateDIBSection(demo->windowDc, &bi, DIB_RGB_COLORS, (void **)&demo->job.displayPtr, NULL, 0);
    assert(hbm);

    demo->memoryDc = CreateCompatibleDC(demo->windowDc);
    assert(demo->memoryDc);

    SelectObject(demo->memoryDc, hbm);
}

static void CALLBACK
DrawTiles(PTP_CALLBACK_INSTANCE instance, void *context, PTP_WORK work)
{
    (void)instance;
    (void)work;
    JobData *data = (JobData *)context;
    uint8_t *displayPtr = data->displayPtr;

    for (;;)
    {
        uint32_t tileIndex = (uint32_t)_InterlockedIncrement((volatile LONG *)s_TileIndex) - 1;
        if (tileIndex >= k_TileCount)
            break;

        uint32_t x0 = (tileIndex % k_TileCountX) * k_TileSize;
        uint32_t y0 = (tileIndex / k_TileCountX) * k_TileSize;
        uint32_t x1 = x0 + k_TileSize;
        uint32_t y1 = y0 + k_TileSize;

        for (uint32_t y = y0; y < y1; ++y)
        {
            for (uint32_t x = x0; x < x1; x += 8)
            {
                uint32_t index = (x + y * k_DemoResolutionX) * 4;
                _mm256_store_si256((__m256i *)&displayPtr[index], _mm256_set1_epi32(0xC0DEC0DE));
            }
        }
    }
}

static void
Initialize(Demo *demo)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    demo->cpuCoreCount = (uint32_t)si.dwNumberOfProcessors;

    demo->jobHandle = CreateThreadpoolWork(DrawTiles, &demo->job, NULL);
    assert(demo->jobHandle);
}

int
main(void)
{
    SetProcessDPIAware();

    Demo demo = { 0 };
    InitializeWindow(&demo);
    Initialize(&demo);

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

            s_TileIndex[0] = 0;

            for (uint32_t i = 0; i < demo.cpuCoreCount; ++i)
                SubmitThreadpoolWork(demo.jobHandle);

            WaitForThreadpoolWorkCallbacks(demo.jobHandle, FALSE);

            BitBlt(demo.windowDc, 0, 0, k_DemoResolutionX, k_DemoResolutionY, demo.memoryDc, 0, 0, SRCCOPY);
        }
    }

    return 0;
}
// vim: ts=4 sw=4 expandtab:
