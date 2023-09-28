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

ID3D12GraphicsCommandList* commandList; // a command list that commands can be recorded into, then be executed frame by frame

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

    // Create the command queue
    D3D12_COMMAND_QUEUE_DESC cqDesc = {}; // all default values

    hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)); // create CQ
    if (FAILED(hr))
    {
        return false;
    }

    // Create the swap chain (with triple buffering)
    DXGI_MODE_DESC backBufferDesc = {};
    backBufferDesc.Width = Width;
    backBufferDesc.Height = Height;
    backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

    // describe multi-sampling (no multi-sampling in this case, therefore -> 1)
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

    // describe and create the swap chain
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferCount = frameBufferCount;                // number of buffers used
    swapChainDesc.BufferDesc = backBufferDesc;                   // back buffer description
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // pipeline will render this to the swapchain
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // dxgi wil discard the buffer (data) after present is called
    swapChainDesc.OutputWindow = hwnd;                           // handle to the window
    swapChainDesc.SampleDesc = sampleDesc;                       // multi-sampling description
    swapChainDesc.Windowed = !FullScreen;

    IDXGISwapChain* tempSwapChain;

    dxgiFactory->CreateSwapChain(
        commandQueue,   // the queue will be flushed once the swap chain is created
        &swapChainDesc, // the swap chain description created above
        &tempSwapChain  // store the created swap chain in a temporary interface
    );

    swapChain = static_cast<IDXGISwapChain3*>(tempSwapChain);

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    // create the back buffers (render target views) descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = frameBufferCount; // number of descriptors for this heap
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

    // the heap will not be referenced by the shaders (not shader visible)
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    hr = device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap));
    if (FAILED(hr))
    {
        return false;
    }

    // get the size of the descriptor in this heap (this may vary depending on device hence we ask the device for the size)
    // size will be used to increment a descriptor handle offset
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // get a handle to the first descriptor in the heap.
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // create a RTV for each buffer
    for (int i = 0; i < frameBufferCount; i++)
    {
        // get buffer i in the swap chain and store it at position i in the ID3D12Resource array
        hr = swapChain->GetBuffer(i, IID_PPV_ARGS(&renderTargets[i]));
        if (FAILED(hr))
        {
            return false;
        }

        // then create a render target view which binds the swapchain buffer to the rtv handle
        device->CreateRenderTargetView(renderTargets[i], nullptr, rtvHandle);

        // increment the rtv handle by the rtv descriptor size from above
        rtvHandle.Offset(1, rtvDescriptorSize);
    }

    // create the command allocators
    for (int i = 0; i < frameBufferCount; i++)
    {
        hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[i]));
        if (FAILED(hr))
        {
            return false;
        }
    }

    // create the command list with the first allocator
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[0], NULL, IID_PPV_ARGS(&commandList));
    if (FAILED(hr))
    {
        return false;
    }

    // command lists are created in the recording state so close it for now as it will be set to recording later when needed
    commandList->Close();

    // create a fence and fence event

    // create the fences
    for (int i = 0; i < frameBufferCount; i++)
    {
        hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i]));
        if (FAILED(hr))
        {
            return false;
        }
        fenceValue[i] = 0; // set the initial fence value to 0;
    }

    // create a handle to a fence handle
    fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (fenceEvent == nullptr)
    {
        return false;
    }

    return true;
}

void Update()
{
    // update app logic, such as moving the camera or figuring out what objects are in view
}

// This function is where commands will be added to the command list, which include changing the state of the render target, 
// setting the root signature and clearing the render target.
// later vertex buffers will be set and draw will be called in this function.
void UpdatePipeline()
{
    HRESULT hr;

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