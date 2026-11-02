/* Draws with a pixel shader that reads SV_Coverage and queries a Texture2DMS, the two things
 * that failed in Trackmania. If its pipeline fails on Metal the draw is lost, and the pixel
 * read back stays the clear colour.
 * usage: msprobe.exe <path to d3dcompiler_47.dll> (Wine's own rejects SV_Coverage, the game's works) */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

static const char vs_src[] =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "  float2 p = float2(id == 2 ? 3.0 : -1.0, id == 1 ? 3.0 : -1.0);\n"
    "  return float4(p, 0.0, 1.0);\n"
    "}\n";

/* mode 0: coverage only, 1: mip query on Texture2DMS only, 2: both, in a branch like the game's. */
static const char *ps_src[] = {
    "float4 main(float4 pos : SV_Position, uint cov : SV_Coverage) : SV_Target {\n"
    "  return float4(firstbitlow(cov) * 0.125, 0.25, 0.5, 1.0);\n"
    "}\n",
    "Texture2DMS<float4> t0 : register(t0);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target {\n"
    "  uint w, h, n;\n"
    "  t0.GetDimensions(w, h, n);\n"
    "  return float4(w == 0 ? 0.0 : 1.0, 0.25, 0.5, 1.0);\n"
    "}\n",
    "Texture2DMS<float4> t0 : register(t0);\n"
    "cbuffer c : register(b0) { uint mode; };\n"
    "float4 main(float4 pos : SV_Position, uint cov : SV_Coverage) : SV_Target {\n"
    "  uint w, h, n;\n"
    "  t0.GetDimensions(w, h, n);\n"
    "  float a = 0.25;\n"
    "  if (mode == 0) a = firstbitlow(cov) * 0.125 + 0.25;\n"
    "  return float4(n * 0.0, a, 0.5, 1.0);\n"
    "}\n",
};

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                     const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static compile_fn compile;

static ID3DBlob *build(const char *src, const char *target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT hr = compile(src, strlen(src), "probe", NULL, NULL, "main", target, 0, 0, &code, &errors);
    if (FAILED(hr))
        printf("compile failed %#lx: %s\n", hr, errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
    return code;
}

int main(int argc, char **argv)
{
    static const FLOAT clear[4] = {1.0f, 0.0f, 1.0f, 1.0f};
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    D3D_FEATURE_LEVEL level;
    D3D11_TEXTURE2D_DESC td = {0};
    ID3D11Texture2D *rt, *staging;
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    D3D11_MAPPED_SUBRESOURCE map;
    HMODULE lib;
    ID3DBlob *blob;
    HRESULT hr;
    int mode;

    if (argc < 2)
        return 2;
    lib = LoadLibraryA(argv[1]);
    compile = (compile_fn)GetProcAddress(lib, "D3DCompile");
    printf("d3dcompiler %p\n", lib);
    if (!compile)
        return 1;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, &level, &context);
    printf("device hr=%#lx level=%#x\n", hr, level);
    if (FAILED(hr))
        return 1;

    td.Width = td.Height = 16;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Device_CreateTexture2D(device, &td, NULL, &rt);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)rt, NULL, &rtv);
    td.BindFlags = 0;
    td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(device, &td, NULL, &staging);

    blob = build(vs_src, "vs_5_0");
    ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), NULL, &vs);

    for (mode = 0; mode < 3; ++mode)
    {
        ID3D11PixelShader *ps;
        unsigned char *pixel;

        if (!(blob = build(ps_src[mode], "ps_5_0")))
            continue;
        ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob), NULL, &ps);

        ID3D11DeviceContext_ClearRenderTargetView(context, rtv, clear);
        ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
        ID3D11DeviceContext_RSSetViewports(context, 1, &vp);
        ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
        ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
        ID3D11DeviceContext_Draw(context, 3, 0);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)rt);
        ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
        pixel = map.pData;
        printf("mode %d pixel=%d,%d,%d,%d %s\n", mode, pixel[0], pixel[1], pixel[2], pixel[3],
               pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 255 ? "DRAW LOST" : "drawn");
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    fflush(stdout);
    ExitProcess(0);
}
