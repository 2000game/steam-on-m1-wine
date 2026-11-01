// dx12probe: D3D12 smoke test for Wine/vkd3d on macOS.
// 1. device + feature report, 2. offscreen triangle + readback,
// 3. compute shader + readback, 4. optional DXIL shaders (from files),
// 5. window + swapchain present loop with FPS.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int g_fail = 0;
#define CHECK(x, what) do { HRESULT _hr = (x); if (FAILED(_hr)) { printf("FAIL %s: hr=0x%08lx\n", what, (unsigned long)_hr); g_fail++; return false; } } while (0)

static ID3D12Device *dev;
static ID3D12CommandQueue *queue;
static ID3D12Fence *fence;
static UINT64 fenceVal;
static HANDLE fenceEvt;

static void waitGpu()
{
    queue->Signal(fence, ++fenceVal);
    if (fence->GetCompletedValue() < fenceVal) { fence->SetEventOnCompletion(fenceVal, fenceEvt); WaitForSingleObject(fenceEvt, 5000); }
}

static ID3DBlob *compile(const char *src, const char *entry, const char *target)
{
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = D3DCompile(src, strlen(src), NULL, NULL, NULL, entry, target, 0, 0, &code, &err);
    if (FAILED(hr)) { printf("FAIL compile %s %s: %s\n", entry, target, err ? (char *)err->GetBufferPointer() : "?"); g_fail++; return NULL; }
    return code;
}

static std::vector<char> readFile(const char *path)
{
    std::vector<char> d; FILE *f = fopen(path, "rb"); if (!f) return d;
    fseek(f, 0, SEEK_END); d.resize(ftell(f)); fseek(f, 0, SEEK_SET); fread(d.data(), 1, d.size(), f); fclose(f); return d;
}

static void features()
{
    D3D_FEATURE_LEVEL fls[] = { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl = { 5, fls };
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) printf("feature_level_max 0x%x\n", fl.MaxSupportedFeatureLevel);
    D3D_SHADER_MODEL sms[] = { (D3D_SHADER_MODEL)0x68, D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1 };
    for (auto s : sms) { D3D12_FEATURE_DATA_SHADER_MODEL sm = { s }; if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) { printf("shader_model_max 0x%x\n", sm.HighestShaderModel); break; } }
    D3D12_FEATURE_DATA_D3D12_OPTIONS o = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))))
        printf("resource_binding_tier %d\ntiled_resources_tier %d\nconservative_raster_tier %d\nrovs %d\ntyped_uav_load_additional %d\nresource_heap_tier %d\n",
               o.ResourceBindingTier, o.TiledResourcesTier, o.ConservativeRasterizationTier, o.ROVsSupported, o.TypedUAVLoadAdditionalFormats, o.ResourceHeapTier);
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof(o1)))) printf("wave_ops %d lanes %u-%u int64 %d\n", o1.WaveOps, o1.WaveLaneCountMin, o1.WaveLaneCountMax, o1.Int64ShaderOps);
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5)))) printf("raytracing_tier %d render_passes_tier %d\n", o5.RaytracingTier, o5.RenderPassesTier);
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 o6 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &o6, sizeof(o6)))) printf("vrs_tier %d\n", o6.VariableShadingRateTier);
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 o7 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &o7, sizeof(o7)))) printf("mesh_shader_tier %d sampler_feedback_tier %d\n", o7.MeshShaderTier, o7.SamplerFeedbackTier);
    else printf("options7 unsupported\n");
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12 = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof(o12)))) printf("enhanced_barriers %d\n", o12.EnhancedBarriersSupported);
    else printf("options12 unsupported\n");
    D3D12_FEATURE_DATA_ARCHITECTURE1 a = {};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &a, sizeof(a)))) printf("uma %d cache_coherent_uma %d\n", a.UMA, a.CacheCoherentUMA);
}

static ID3D12Resource *buffer(UINT64 size, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES st, D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
    D3D12_HEAP_PROPERTIES hp = { type };
    D3D12_RESOURCE_DESC rd = {}; rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = flags;
    ID3D12Resource *r = NULL;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, NULL, IID_PPV_ARGS(&r)))) { printf("FAIL buffer\n"); g_fail++; }
    return r;
}

static void barrier(ID3D12GraphicsCommandList *cl, ID3D12Resource *r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b)
{
    D3D12_RESOURCE_BARRIER rb = {}; rb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; rb.Transition.pResource = r; rb.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; rb.Transition.StateBefore = a; rb.Transition.StateAfter = b;
    cl->ResourceBarrier(1, &rb);
}

static const char *kGfx =
    "float4 vs(uint id : SV_VertexID) : SV_Position { float2 p[3] = { float2(-1,-1), float2(0,3), float2(3,-1) }; return float4(p[id] * 0.9, 0, 1); }\n"
    "float4 ps() : SV_Target { return float4(0.25, 0.5, 0.75, 1.0); }\n";
static const char *kCs =
    "RWStructuredBuffer<uint> o : register(u0);\n"
    "[numthreads(64,1,1)] void cs(uint i : SV_DispatchThreadID) { o[i] = i * i + 7; }\n";

// Offscreen triangle: compiled VS/PS, render to 64x64, read back centre pixel.
static bool triangle(const void *vs, SIZE_T vsn, const void *ps, SIZE_T psn, const char *label)
{
    ID3D12CommandAllocator *ca; ID3D12GraphicsCommandList *cl;
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)), "allocator");
    D3D12_ROOT_SIGNATURE_DESC rsd = {}; rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ID3DBlob *rsb, *err; ID3D12RootSignature *rs;
    CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, &err), "serialize rs");
    CHECK(dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), IID_PPV_ARGS(&rs)), "root signature");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rs; pd.VS = { vs, vsn }; pd.PS = { ps, psn };
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0xf;
    pd.SampleMask = ~0u; pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; pd.RasterizerState.DepthClipEnable = TRUE;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; pd.NumRenderTargets = 1; pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; pd.SampleDesc.Count = 1;
    ID3D12PipelineState *pso;
    CHECK(dev->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), "graphics PSO");
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, pso, IID_PPV_ARGS(&cl)), "command list");

    D3D12_HEAP_PROPERTIES hp = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = 64; td.Height = 64; td.DepthOrArraySize = 1; td.MipLevels = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1; td.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource *rt;
    CHECK(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_RENDER_TARGET, NULL, IID_PPV_ARGS(&rt)), "render target");
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1 };
    ID3D12DescriptorHeap *rtvh; CHECK(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvh)), "rtv heap");
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvh->GetCPUDescriptorHandleForHeapStart();
    dev->CreateRenderTargetView(rt, NULL, rtv);
    ID3D12Resource *rb = buffer(256 * 64, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);

    float clear[4] = { 1, 0, 0, 1 };
    cl->ClearRenderTargetView(rtv, clear, 0, NULL);
    cl->OMSetRenderTargets(1, &rtv, FALSE, NULL);
    D3D12_VIEWPORT vp = { 0, 0, 64, 64, 0, 1 }; D3D12_RECT sc = { 0, 0, 64, 64 };
    cl->RSSetViewports(1, &vp); cl->RSSetScissorRects(1, &sc);
    cl->SetGraphicsRootSignature(rs); cl->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cl->DrawInstanced(3, 1, 0, 0);
    barrier(cl, rt, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst = {}; dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, 64, 64, 1, 256 };
    D3D12_TEXTURE_COPY_LOCATION src = {}; src.pResource = rt; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &src, NULL);
    cl->Close();
    ID3D12CommandList *lists[] = { cl }; queue->ExecuteCommandLists(1, lists); waitGpu();
    unsigned char *p; D3D12_RANGE rr = { 0, 256 * 64 };
    CHECK(rb->Map(0, &rr, (void **)&p), "map readback");
    unsigned char *c = p + 32 * 256 + 32 * 4, *e = p;
    bool ok = abs(c[0] - 64) <= 2 && abs(c[1] - 128) <= 2 && abs(c[2] - 191) <= 2 && e[0] == 255 && e[1] == 0;
    printf("%s triangle_%s centre=%d,%d,%d corner=%d,%d,%d\n", ok ? "PASS" : "FAIL", label, c[0], c[1], c[2], e[0], e[1], e[2]);
    if (!ok) g_fail++;
    rb->Unmap(0, NULL);
    return ok;
}

static bool compute(const void *cs, SIZE_T csn, const char *label)
{
    D3D12_DESCRIPTOR_RANGE r = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, 0 };
    D3D12_ROOT_PARAMETER rp = {}; rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; rp.Descriptor.ShaderRegister = 0; rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    (void)r;
    D3D12_ROOT_SIGNATURE_DESC rsd = { 1, &rp, 0, NULL, D3D12_ROOT_SIGNATURE_FLAG_NONE };
    ID3DBlob *rsb, *err; ID3D12RootSignature *rs;
    CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsb, &err), "serialize cs rs");
    CHECK(dev->CreateRootSignature(0, rsb->GetBufferPointer(), rsb->GetBufferSize(), IID_PPV_ARGS(&rs)), "cs root signature");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = rs; pd.CS = { cs, csn };
    ID3D12PipelineState *pso; CHECK(dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso)), "compute PSO");
    ID3D12CommandAllocator *ca; ID3D12GraphicsCommandList *cl;
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)), "cs allocator");
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, pso, IID_PPV_ARGS(&cl)), "cs list");
    const UINT n = 4096;
    ID3D12Resource *ub = buffer(n * 4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Resource *rb = buffer(n * 4, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    cl->SetComputeRootSignature(rs); cl->SetComputeRootUnorderedAccessView(0, ub->GetGPUVirtualAddress());
    cl->Dispatch(n / 64, 1, 1);
    barrier(cl, ub, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cl->CopyResource(rb, ub); cl->Close();
    ID3D12CommandList *lists[] = { cl }; queue->ExecuteCommandLists(1, lists); waitGpu();
    UINT *p; D3D12_RANGE rr = { 0, n * 4 }; CHECK(rb->Map(0, &rr, (void **)&p), "map cs");
    UINT bad = 0; for (UINT i = 0; i < n; i++) if (p[i] != i * i + 7) bad++;
    printf("%s compute_%s bad=%u/%u\n", bad ? "FAIL" : "PASS", label, bad, n);
    if (bad) g_fail++;
    rb->Unmap(0, NULL);
    return !bad;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

// Window + flip swapchain, clear to a cycling colour, measure presents/s.
static bool presentLoop(IDXGIFactory4 *fac, int seconds, UINT interval)
{
    WNDCLASSW wc = {}; wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"dx12probe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(L"dx12probe", L"dx12probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1280, 720, NULL, NULL, wc.hInstance, NULL);
    DXGI_SWAP_CHAIN_DESC1 sd = {}; sd.Width = 1280; sd.Height = 720; sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count = 1; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount = 3; sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1 *sc1; CHECK(fac->CreateSwapChainForHwnd(queue, hwnd, &sd, NULL, NULL, &sc1), "swapchain");
    IDXGISwapChain3 *sc; CHECK(sc1->QueryInterface(IID_PPV_ARGS(&sc)), "swapchain3");
    D3D12_DESCRIPTOR_HEAP_DESC hd = { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3 };
    ID3D12DescriptorHeap *rtvh; CHECK(dev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvh)), "sc rtv heap");
    UINT inc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource *bb[3]; D3D12_CPU_DESCRIPTOR_HANDLE rtv[3];
    for (int i = 0; i < 3; i++) { CHECK(sc->GetBuffer(i, IID_PPV_ARGS(&bb[i])), "getbuffer"); rtv[i] = rtvh->GetCPUDescriptorHandleForHeapStart(); rtv[i].ptr += i * inc; dev->CreateRenderTargetView(bb[i], NULL, rtv[i]); }
    ID3D12CommandAllocator *ca; ID3D12GraphicsCommandList *cl;
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ca)), "loop allocator");
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ca, NULL, IID_PPV_ARGS(&cl)), "loop list");
    cl->Close();
    LARGE_INTEGER f, t0, t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
    int frames = 0;
    for (;;) {
        MSG msg; while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        QueryPerformanceCounter(&t); double el = (double)(t.QuadPart - t0.QuadPart) / f.QuadPart;
        if (el >= seconds) { printf("PASS present_interval%u frames=%d fps=%.1f\n", interval, frames, frames / el); break; }
        waitGpu(); ca->Reset(); cl->Reset(ca, NULL);
        UINT i = sc->GetCurrentBackBufferIndex();
        barrier(cl, bb[i], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        float c[4] = { (frames % 120) / 120.f, 0.3f, 0.6f, 1 }; cl->ClearRenderTargetView(rtv[i], c, 0, NULL);
        barrier(cl, bb[i], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        cl->Close(); ID3D12CommandList *lists[] = { cl }; queue->ExecuteCommandLists(1, lists);
        HRESULT hr = sc->Present(interval, interval ? 0 : DXGI_PRESENT_ALLOW_TEARING * 0);
        if (FAILED(hr)) { printf("FAIL present hr=0x%08lx\n", (unsigned long)hr); g_fail++; break; }
        frames++;
    }
    waitGpu();
    sc->Release(); DestroyWindow(hwnd);
    return true;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int seconds = argc > 1 ? atoi(argv[1]) : 5;
    IDXGIFactory4 *fac;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&fac)))) { printf("FAIL dxgi factory\n"); return 2; }
    IDXGIAdapter1 *ad; DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(fac->EnumAdapters1(0, &ad))) { ad->GetDesc1(&desc); printf("adapter %ls vram=%lluMB\n", desc.Description, (unsigned long long)desc.DedicatedVideoMemory >> 20); }
    HRESULT hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev));
    if (FAILED(hr)) { printf("FAIL D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr); return 3; }
    printf("PASS device\n");
    D3D12_COMMAND_QUEUE_DESC qd = { D3D12_COMMAND_LIST_TYPE_DIRECT };
    dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));
    dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)); fenceEvt = CreateEventW(NULL, FALSE, FALSE, NULL);
    features();

    ID3DBlob *vs = compile(kGfx, "vs", "vs_5_0"), *ps = compile(kGfx, "ps", "ps_5_0"), *cs = compile(kCs, "cs", "cs_5_0");
    if (vs && ps) triangle(vs->GetBufferPointer(), vs->GetBufferSize(), ps->GetBufferPointer(), ps->GetBufferSize(), "dxbc");
    if (cs) compute(cs->GetBufferPointer(), cs->GetBufferSize(), "dxbc");

    // DXIL (SM 6.x) shaders, precompiled with dxc into the exe's folder.
    std::vector<char> dvs = readFile("vs.dxil"), dps = readFile("ps.dxil"), dcs = readFile("cs.dxil"), dcs66 = readFile("cs66.dxil");
    if (!dvs.empty() && !dps.empty()) triangle(dvs.data(), dvs.size(), dps.data(), dps.size(), "dxil");
    else printf("SKIP dxil shaders not found\n");
    if (!dcs.empty()) compute(dcs.data(), dcs.size(), "dxil");
    if (!dcs66.empty()) compute(dcs66.data(), dcs66.size(), "dxil_sm66_wave");

    if (seconds > 0) { presentLoop(fac, seconds, 1); presentLoop(fac, seconds, 0); }
    printf("RESULT failures=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
