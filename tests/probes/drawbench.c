/* Measures what a frame costs through DXVK, MoltenVK and Metal, without a window.
 * Each draw updates constants and binds textures, as a game's do, and the triangles are small,
 * so the result shows the cost of the graphics stack on the CPU and not the GPU's fill rate.
 * usage: drawbench.exe <draws per frame> <frames> */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1800
#define HEIGHT 1169
#define TEXTURES 16

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                     const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char vs_src[] =
    "cbuffer c : register(b0) { float4 offset; float4x4 world; };\n"
    "struct Out { float4 position : SV_Position; float2 uv : TEXCOORD0; };\n"
    "Out main(uint id : SV_VertexID) {\n"
    "  Out o;\n"
    "  float2 p = float2(id & 1, (id >> 1) & 1);\n"
    "  o.uv = p;\n"
    "  o.position = mul(world, float4(p * 0.01 + offset.xy, 0.5, 1.0));\n"
    "  return o;\n"
    "}\n";

static const char ps_src[] =
    "cbuffer c : register(b0) { float4 tint; };\n"
    "Texture2D a : register(t0); Texture2D b : register(t1); Texture2D c : register(t2); Texture2D d : register(t3);\n"
    "SamplerState s : register(s0);\n"
    "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
    "  return tint * (a.Sample(s, uv) + b.Sample(s, uv) + c.Sample(s, uv) * d.Sample(s, uv));\n"
    "}\n";

static compile_fn compile;

static ID3DBlob *build(const char *src, const char *target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    if (FAILED(compile(src, strlen(src), "drawbench", NULL, NULL, "main", target, 0, 0, &code, &errors)))
    {
        printf("compile failed: %s\n", errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
        ExitProcess(1);
    }
    return code;
}

static ID3D11Buffer *constant_buffer(ID3D11Device *device, UINT size)
{
    D3D11_BUFFER_DESC desc = {size, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0};
    ID3D11Buffer *buffer;
    ID3D11Device_CreateBuffer(device, &desc, NULL, &buffer);
    return buffer;
}

static int compare(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

int main(int argc, char **argv)
{
    int draws = argc > 1 ? atoi(argv[1]) : 3000, frames = argc > 2 ? atoi(argv[2]) : 300;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    D3D_FEATURE_LEVEL level;
    LARGE_INTEGER frequency, begin, recorded, done;
    ID3D11ShaderResourceView *views[TEXTURES];
    double *record_ms = calloc(frames, sizeof(double)), *frame_ms = calloc(frames, sizeof(double));

    QueryPerformanceFrequency(&frequency);
    compile = (compile_fn)GetProcAddress(LoadLibraryA("d3dcompiler_47.dll"), "D3DCompile");
    if (!compile || FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                             &device, &level, &context)))
        return 1;

    D3D11_TEXTURE2D_DESC target_desc = {WIDTH, HEIGHT, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {4, 0}, D3D11_USAGE_DEFAULT,
                                        D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *target, *depth, *resolved;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11Device_CreateTexture2D(device, &target_desc, NULL, &target);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    D3D11_TEXTURE2D_DESC depth_desc = target_desc;
    depth_desc.Format = DXGI_FORMAT_D32_FLOAT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Device_CreateTexture2D(device, &depth_desc, NULL, &depth);
    ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)depth, NULL, &dsv);
    target_desc.SampleDesc.Count = 1;
    target_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Device_CreateTexture2D(device, &target_desc, NULL, &resolved);

    static unsigned pixels[64 * 64];
    for (int i = 0; i < TEXTURES; i++)
    {
        D3D11_TEXTURE2D_DESC desc = {64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                                     D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA data = {pixels, 64 * 4, 0};
        ID3D11Texture2D *texture;
        memset(pixels, 40 + i, sizeof(pixels));
        ID3D11Device_CreateTexture2D(device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)texture, NULL, &views[i]);
    }

    ID3DBlob *vs_blob = build(vs_src, "vs_5_0"), *ps_blob = build(ps_src, "ps_5_0");
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &vs);
    ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &ps);

    ID3D11Buffer *vs_constants = constant_buffer(device, 80), *ps_constants = constant_buffer(device, 16);
    D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
                                       D3D11_TEXTURE_ADDRESS_WRAP, 0, 1, D3D11_COMPARISON_NEVER, {0}, 0, D3D11_FLOAT32_MAX};
    ID3D11SamplerState *sampler;
    ID3D11Device_CreateSamplerState(device, &sampler_desc, &sampler);
    D3D11_BLEND_DESC blend_desc = {0};
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState *blends[2];
    ID3D11Device_CreateBlendState(device, &blend_desc, &blends[0]);
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].BlendOp = blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ID3D11Device_CreateBlendState(device, &blend_desc, &blends[1]);

    D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
    ID3D11Query *query;
    ID3D11Device_CreateQuery(device, &query_desc, &query);
    D3D11_VIEWPORT viewport = {0, 0, WIDTH, HEIGHT, 0, 1};
    static const FLOAT clear[4] = {0.1f, 0.2f, 0.3f, 1.0f};

    /* The first frames compile the pipelines. They are left out of the result. */
    for (int frame = -30; frame < frames; frame++)
    {
        QueryPerformanceCounter(&begin);
        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, clear);
        ID3D11DeviceContext_ClearDepthStencilView(context, dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, dsv);
        ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
        ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
        ID3D11DeviceContext_VSSetConstantBuffers(context, 0, 1, &vs_constants);
        ID3D11DeviceContext_PSSetConstantBuffers(context, 0, 1, &ps_constants);
        ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &sampler);
        for (int i = 0; i < draws; i++)
        {
            D3D11_MAPPED_SUBRESOURCE map;
            float *out;
            ID3D11DeviceContext_Map(context, (ID3D11Resource *)vs_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            out = map.pData;
            memset(out, 0, 80);
            out[0] = (i % 97) / 50.0f - 1.0f;
            out[1] = (i % 89) / 45.0f - 1.0f;
            out[4] = out[9] = out[14] = out[19] = 1.0f;
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)vs_constants, 0);
            ID3D11DeviceContext_Map(context, (ID3D11Resource *)ps_constants, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
            out = map.pData;
            out[0] = out[1] = out[2] = out[3] = 0.25f;
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)ps_constants, 0);
            ID3D11DeviceContext_PSSetShaderResources(context, 0, 4, &views[(i * 3) % (TEXTURES - 3)]);
            if (!(i % 16))
                ID3D11DeviceContext_OMSetBlendState(context, blends[(i >> 4) & 1], NULL, ~0u);
            ID3D11DeviceContext_Draw(context, 4, 0);
        }
        ID3D11DeviceContext_ResolveSubresource(context, (ID3D11Resource *)resolved, 0, (ID3D11Resource *)target, 0,
                                               DXGI_FORMAT_R8G8B8A8_UNORM);
        ID3D11DeviceContext_End(context, (ID3D11Asynchronous *)query);
        ID3D11DeviceContext_Flush(context);
        QueryPerformanceCounter(&recorded);
        while (ID3D11DeviceContext_GetData(context, (ID3D11Asynchronous *)query, NULL, 0, 0) == S_FALSE)
            SwitchToThread();
        QueryPerformanceCounter(&done);
        if (frame >= 0)
        {
            record_ms[frame] = (recorded.QuadPart - begin.QuadPart) * 1000.0 / frequency.QuadPart;
            frame_ms[frame] = (done.QuadPart - begin.QuadPart) * 1000.0 / frequency.QuadPart;
        }
    }
    /* The picture has to come out the same under every setting. */
    D3D11_TEXTURE2D_DESC staging_desc = target_desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D *staging;
    D3D11_MAPPED_SUBRESOURCE pixels_map;
    unsigned long long sum = 14695981039346656037ull;
    ID3D11Device_CreateTexture2D(device, &staging_desc, NULL, &staging);
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)resolved);
    ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &pixels_map);
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            sum = (sum ^ ((unsigned *)((char *)pixels_map.pData + y * pixels_map.RowPitch))[x]) * 1099511628211ull;
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);

    qsort(record_ms, frames, sizeof(double), compare);
    qsort(frame_ms, frames, sizeof(double), compare);
    printf("draws %d frame median %.2f ms p95 %.2f ms record median %.2f ms per draw %.2f us image %016llx\n", draws,
           frame_ms[frames / 2], frame_ms[frames * 95 / 100], record_ms[frames / 2], frame_ms[frames / 2] * 1000 / draws, sum);
    fflush(stdout);
    ExitProcess(0);
}
