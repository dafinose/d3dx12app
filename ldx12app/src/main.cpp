#include "../header/stdafx.h"

// Handle to the window
HWND hwnd = NULL;

// name of the window (not the title)
LPCTSTR WindowName = L"DX12_App";

// title of the window
LPCTSTR WindowTitle = L"DX12 Application";

// width and height of the window
int Width = 800;
int Height = 600;

bool FullScreen = false;

// create a window
bool InitializeWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool fullscreen);

// main application loop
void mainloop();

// callback function for windows messages
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

//-------------------------------------------------------------------------
// Direct 3D Stuff

const int frameBufferCount = 3; // number of buffers, since triple buffering is recommended this is 3

ID3D12Device* device; // Direct3D device pointer

IDXGISwapChain3* swapChain; // swap chain used to switch between render targets

ID3D12CommandQueue* commandQueue; // container for command lists

ID3D12DescriptorHeap* rtvDescriptorHeap; // a descriptor heap to hold resources like the render targets

ID3D12Resource* renderTargets[frameBufferCount]; // number of render targets (equal to framebuffer count)

ID3D12CommandAllocator* commandAllocator[frameBufferCount]; // enough allocators for each buffer x number of threads (3 here since only 1 thread)

ID3D12CommandList* commandList; // a command list that commands can be recorded into, then be executed frame by frame

ID3D12Fence* fence[frameBufferCount]; // an object that is locked while the command list is being executed by the GPU

HANDLE fenceEvent; // a handle to an event when the fence is unlocked by the GPU

UINT64 fenceValue[frameBufferCount]; // this value is incremented each frame, each fence will have its own value

int frameIndex; // current RTV the application is on

int rtvDescriptorSize; // size of the RTV descriptor on the device (all front and back buffers will be the same size)

// Function declarations
bool InitD3D(); // initilization of direct3d 12

void Update(); // update the game logic

void UpdatePipeline(); // update the direct3d pipeline (i.e. the command lists)

void Render(); // execute command list

void Cleanup(); // cleanup the GPU stuff before exiting the application

void WaitForPreviousFrame(); // wait until gpu is finished with command list

//-------------------------------------------------------------------------

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nShowCmd)
{
    // initialize Direct3D
    if (!InitD3D())
    {
        MessageBox(0, L"Failed to initialize Direct3D 12", L"Error", MB_OK);
        Cleanup();
        return 1;
    }

    // create the window
    if (!InitializeWindow(hInstance, nShowCmd, Width, Height, FullScreen))
    {
        MessageBox(0, L"Window Initialization - Failed",
            L"Error", MB_OK);
        return 0;
    }

    // start the main loop
    mainloop();

    return 0;
}

bool InitializeWindow(HINSTANCE hInstance,
    int ShowWnd,
    int width, int height,
    bool fullscreen)
{
    if (fullscreen)
    {
        HMONITOR hmon = MonitorFromWindow(hwnd,
            MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hmon, &mi);

        width = mi.rcMonitor.right - mi.rcMonitor.left;
        height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    WNDCLASSEX wc;

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = NULL;
    wc.cbWndExtra = NULL;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = WindowName;
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc))
    {
        MessageBox(NULL, L"Error registering class",
            L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    hwnd = CreateWindowEx(NULL,
        WindowName,
        WindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd)
    {
        MessageBox(NULL, L"Error creating window",
            L"Error", MB_OK | MB_ICONERROR);
        return false;
    }

    if (fullscreen)
    {
        SetWindowLong(hwnd, GWL_STYLE, 0);
    }

    ShowWindow(hwnd, ShowWnd);
    UpdateWindow(hwnd);

    return true;
}

void mainloop() {
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));

    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            // run game code
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (msg)
    {

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (MessageBox(0, L"Are you sure you want to exit?",
                L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
                DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd,
        msg,
        wParam,
        lParam);
}

bool InitD3D()
{
    HRESULT hr;

    // -- Create the Device -- //

    IDXGIFactory4* dxgiFactory;
    hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr))
    {
        return false;
    }

    // first create the direct3d 12 device, query for the first physical device
    IDXGIAdapter1* adapter; // adapters are the graphics cards on the device (including onboard cards)

    int adapterIndex = 0; // start from 0 and work upwards

    bool adapterFound = false; // true as soon as a compatible one is found

    while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            // don't want to use a software device so continue
            adapterIndex++;
            continue;
        }

        hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        if (SUCCEEDED(hr))
        {
            adapterFound = true;
            break;
        }

        adapterIndex++;
    }

    if (!adapterFound)
    { 
        return false;
    }

    // Create the device
    hr = D3D12CreateDevice(
        adapter,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&device)
    );
    if (FAILED(hr))
    {
        return false;
    }

    return 0;
}

void Update()
{
}

void UpdatePipeline()
{
}

void Render()
{
}

void Cleanup()
{
}

void WaitForPreviousFrame()
{
}