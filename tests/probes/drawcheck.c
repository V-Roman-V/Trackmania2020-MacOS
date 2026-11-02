/* Draws a fixed scene with every kind of call that winevulkan queues, and prints a checksum of
 * the picture and of a compute result. It has to come out the same with WINEVULKAN_NO_BATCH=1. */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

#define SIZE 256
#define PASSES 400

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                     const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char vs_src[] =
    "cbuffer c : register(b0) { float4 offset; };\n"
    "struct In { float2 position : POSITION; float2 uv : TEXCOORD0; float4 tint : COLOR0; float2 shift : TEXCOORD1; };\n"
    "struct Out { float4 position : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; };\n"
    "Out main(In v) {\n"
    "  Out o;\n"
    "  o.position = float4(v.position * 0.2 + v.shift + offset.xy, offset.z, 1.0);\n"
    "  o.uv = v.uv;\n"
    "  o.tint = v.tint * offset.w;\n"
    "  return o;\n"
    "}\n";

static const char ps_src[] =
    "cbuffer c : register(b0) { float4 tint; };\n"
    "Texture2D a : register(t0); Texture2D b : register(t1);\n"
    "SamplerState s : register(s0);\n"
    "float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target {\n"
    "  return tint * color * (a.Sample(s, uv) + b.Sample(s, uv));\n"
    "}\n";

static const char cs_src[] =
    "cbuffer c : register(b0) { uint base; };\n"
    "RWStructuredBuffer<uint> out_buffer : register(u0);\n"
    "[numthreads(4, 1, 1)]\n"
    "void main(uint3 id : SV_DispatchThreadID) { out_buffer[id.x] += id.x * base + 1; }\n";

struct vertex
{
    float position[2], uv[2], tint[4];
};

static compile_fn compile;
static ID3D11Device *device;
static ID3D11DeviceContext *context;

static ID3DBlob *build(const char *src, const char *target)
{
    ID3DBlob *code = NULL, *errors = NULL;
    if (FAILED(compile(src, strlen(src), "drawcheck", NULL, NULL, "main", target, 0, 0, &code, &errors)))
    {
        printf("compile failed: %s\n", errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
        ExitProcess(1);
    }
    return code;
}

static ID3D11Buffer *buffer(UINT size, D3D11_USAGE usage, UINT bind, UINT access, UINT misc, UINT stride, const void *data)
{
    D3D11_BUFFER_DESC desc = {size, usage, bind, access, misc, stride};
    D3D11_SUBRESOURCE_DATA init = {data, 0, 0};
    ID3D11Buffer *result;
    ID3D11Device_CreateBuffer(device, &desc, data ? &init : NULL, &result);
    return result;
}

static void set_constants(ID3D11Buffer *target, const void *data, UINT size)
{
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11DeviceContext_Map(context, (ID3D11Resource *)target, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
    memcpy(map.pData, data, size);
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)target, 0);
}

static unsigned long long checksum(ID3D11Resource *source, D3D11_RESOURCE_DIMENSION kind, UINT bytes)
{
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Resource *staging;
    unsigned long long sum = 14695981039346656037ull;

    if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        D3D11_TEXTURE2D_DESC desc;
        ID3D11Texture2D_GetDesc((ID3D11Texture2D *)source, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.SampleDesc.Count = 1;
        desc.MiscFlags = 0;
        ID3D11Device_CreateTexture2D(device, &desc, NULL, (ID3D11Texture2D **)&staging);
    }
    else
    {
        D3D11_BUFFER_DESC desc = {bytes, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
        ID3D11Device_CreateBuffer(device, &desc, NULL, (ID3D11Buffer **)&staging);
    }
    ID3D11DeviceContext_CopyResource(context, staging, source);
    ID3D11DeviceContext_Map(context, staging, 0, D3D11_MAP_READ, 0, &map);
    if (kind == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    {
        for (int y = 0; y < SIZE; y++)
            for (int x = 0; x < SIZE; x++)
                sum = (sum ^ ((unsigned *)((char *)map.pData + y * map.RowPitch))[x]) * 1099511628211ull;
    }
    else
    {
        for (UINT i = 0; i < bytes / 4; i++)
            sum = (sum ^ ((unsigned *)map.pData)[i]) * 1099511628211ull;
    }
    ID3D11DeviceContext_Unmap(context, staging, 0);
    return sum;
}

int main(void)
{
    D3D_FEATURE_LEVEL level;

    compile = (compile_fn)GetProcAddress(LoadLibraryA("d3dcompiler_47.dll"), "D3DCompile");
    if (!compile || FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                             &device, &level, &context)))
        return 1;

    D3D11_TEXTURE2D_DESC target_desc = {SIZE, SIZE, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                        D3D11_BIND_RENDER_TARGET, 0, 0};
    ID3D11Texture2D *target, *depth;
    ID3D11RenderTargetView *rtv;
    ID3D11DepthStencilView *dsv;
    ID3D11Device_CreateTexture2D(device, &target_desc, NULL, &target);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    D3D11_TEXTURE2D_DESC depth_desc = target_desc;
    depth_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Device_CreateTexture2D(device, &depth_desc, NULL, &depth);
    ID3D11Device_CreateDepthStencilView(device, (ID3D11Resource *)depth, NULL, &dsv);

    static const struct vertex quad[4] = {
        {{-1, -1}, {0, 1}, {1, 0.5f, 0.25f, 1}}, {{-1, 1}, {0, 0}, {0.25f, 1, 0.5f, 1}},
        {{1, -1}, {1, 1}, {0.5f, 0.25f, 1, 1}}, {{1, 1}, {1, 0}, {1, 1, 1, 1}},
    };
    static const unsigned short indices[6] = {0, 1, 2, 2, 1, 3};
    float shifts[8][2];
    for (int i = 0; i < 8; i++)
    {
        shifts[i][0] = (i % 4) * 0.25f - 0.4f;
        shifts[i][1] = (i / 4) * 0.5f - 0.3f;
    }
    ID3D11Buffer *vertices = buffer(sizeof(quad), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, quad);
    ID3D11Buffer *instances = buffer(sizeof(shifts), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, shifts);
    ID3D11Buffer *index_buffer = buffer(sizeof(indices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0, indices);
    ID3D11Buffer *vs_constants = buffer(16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, NULL);
    ID3D11Buffer *ps_constants = buffer(16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, NULL);
    ID3D11Buffer *cs_constants = buffer(16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, NULL);
    ID3D11Buffer *structured = buffer(64 * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                      D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, NULL);
    ID3D11UnorderedAccessView *uav;
    ID3D11Device_CreateUnorderedAccessView(device, (ID3D11Resource *)structured, NULL, &uav);

    ID3D11ShaderResourceView *views[6];
    static unsigned pixels[16 * 16];
    for (int i = 0; i < 6; i++)
    {
        D3D11_TEXTURE2D_DESC desc = {16, 16, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                                     D3D11_BIND_SHADER_RESOURCE, 0, 0};
        D3D11_SUBRESOURCE_DATA data = {pixels, 16 * 4, 0};
        ID3D11Texture2D *texture;
        for (int p = 0; p < 16 * 16; p++)
            pixels[p] = 0xff000000 | (p * (i + 3) * 2654435761u >> 8);
        ID3D11Device_CreateTexture2D(device, &desc, &data, &texture);
        ID3D11Device_CreateShaderResourceView(device, (ID3D11Resource *)texture, NULL, &views[i]);
    }

    ID3DBlob *vs_blob = build(vs_src, "vs_5_0"), *ps_blob = build(ps_src, "ps_5_0"), *cs_blob = build(cs_src, "cs_5_0");
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11ComputeShader *cs;
    ID3D11InputLayout *layout;
    ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob), NULL, &vs);
    ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob), NULL, &ps);
    ID3D11Device_CreateComputeShader(device, ID3D10Blob_GetBufferPointer(cs_blob), ID3D10Blob_GetBufferSize(cs_blob), NULL, &cs);
    static const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1, 0, D3D11_INPUT_PER_INSTANCE_DATA, 1},
    };
    ID3D11Device_CreateInputLayout(device, elements, ARRAYSIZE(elements), ID3D10Blob_GetBufferPointer(vs_blob),
                                   ID3D10Blob_GetBufferSize(vs_blob), &layout);

    D3D11_SAMPLER_DESC sampler_desc = {D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_TEXTURE_ADDRESS_WRAP,
                                       D3D11_TEXTURE_ADDRESS_WRAP, 0, 1, D3D11_COMPARISON_NEVER, {0}, 0, D3D11_FLOAT32_MAX};
    ID3D11SamplerState *sampler;
    ID3D11Device_CreateSamplerState(device, &sampler_desc, &sampler);

    D3D11_BLEND_DESC blend_desc = {0};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_BLEND_FACTOR;
    blend_desc.RenderTarget[0].DestBlend = blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_BLEND_FACTOR;
    blend_desc.RenderTarget[0].BlendOp = blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11BlendState *blend;
    ID3D11Device_CreateBlendState(device, &blend_desc, &blend);

    D3D11_DEPTH_STENCIL_DESC stencil_desc = {0};
    stencil_desc.DepthEnable = TRUE;
    stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    stencil_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    stencil_desc.StencilEnable = TRUE;
    stencil_desc.StencilReadMask = stencil_desc.StencilWriteMask = 0xff;
    stencil_desc.FrontFace.StencilFunc = stencil_desc.BackFace.StencilFunc = D3D11_COMPARISON_GREATER_EQUAL;
    stencil_desc.FrontFace.StencilPassOp = stencil_desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_REPLACE;
    stencil_desc.FrontFace.StencilFailOp = stencil_desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    stencil_desc.FrontFace.StencilDepthFailOp = stencil_desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    ID3D11DepthStencilState *stencil;
    if (FAILED(ID3D11Device_CreateDepthStencilState(device, &stencil_desc, &stencil)))
        return 2;

    ID3D11RasterizerState *rasterizers[2];
    for (int i = 0; i < 2; i++)
    {
        D3D11_RASTERIZER_DESC desc = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, i ? 500 : 0, 0.0f, i ? 1.5f : 0.0f, TRUE, TRUE, FALSE, FALSE};
        ID3D11Device_CreateRasterizerState(device, &desc, &rasterizers[i]);
    }

    static const FLOAT clear[4] = {0.05f, 0.1f, 0.15f, 1.0f};
    UINT strides[2] = {sizeof(struct vertex), sizeof(float) * 2}, offsets[2] = {0, 0};
    ID3D11Buffer *streams[2] = {vertices, instances};

    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, clear);
    ID3D11DeviceContext_ClearDepthStencilView(context, dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, dsv);
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(context, layout);
    ID3D11DeviceContext_IASetVertexBuffers(context, 0, 2, streams, strides, offsets);
    ID3D11DeviceContext_IASetIndexBuffer(context, index_buffer, DXGI_FORMAT_R16_UINT, 0);
    ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(context, 0, 1, &vs_constants);
    ID3D11DeviceContext_PSSetConstantBuffers(context, 0, 1, &ps_constants);
    ID3D11DeviceContext_PSSetSamplers(context, 0, 1, &sampler);

    for (int pass = 0; pass < PASSES; pass++)
    {
        float vs_values[4] = {(pass % 7) * 0.05f - 0.15f, (pass % 11) * 0.03f - 0.15f, (pass % 13) / 13.0f, 0.5f + (pass % 5) * 0.1f};
        float ps_values[4] = {0.5f, 0.6f, 0.7f, 1.0f};
        FLOAT factor[4] = {(pass % 3 + 1) * 0.2f, (pass % 4 + 1) * 0.15f, (pass % 5 + 1) * 0.1f, 1.0f};
        D3D11_VIEWPORT viewport = {(FLOAT)(pass % 16), (FLOAT)(pass % 9), SIZE - 32, SIZE - 24, 0, 1};
        D3D11_RECT scissor = {40 + pass % 64, 30 + pass % 47, 190 - pass % 39, 200 - pass % 43};

        set_constants(vs_constants, vs_values, sizeof(vs_values));
        set_constants(ps_constants, ps_values, sizeof(ps_values));
        ID3D11DeviceContext_RSSetViewports(context, 1, &viewport);
        ID3D11DeviceContext_RSSetScissorRects(context, 1, &scissor);
        ID3D11DeviceContext_RSSetState(context, rasterizers[pass & 1]);
        ID3D11DeviceContext_OMSetBlendState(context, blend, factor, ~0u);
        ID3D11DeviceContext_OMSetDepthStencilState(context, stencil, pass % 200);
        ID3D11DeviceContext_PSSetShaderResources(context, 0, 2, &views[pass % 5]);

        if (pass & 2)
            ID3D11DeviceContext_DrawIndexedInstanced(context, 6, pass % 8 + 1, 0, 0, pass % 4);
        else if (pass & 4)
            ID3D11DeviceContext_DrawInstanced(context, 3, 1, 0, pass % 8);
        else
            ID3D11DeviceContext_DrawIndexed(context, 6, 0, 0);

        if (!(pass % 16))
        {
            unsigned base[4] = {(unsigned)pass, 0, 0, 0};
            set_constants(cs_constants, base, sizeof(base));
            ID3D11DeviceContext_CSSetShader(context, cs, NULL, 0);
            ID3D11DeviceContext_CSSetConstantBuffers(context, 0, 1, &cs_constants);
            ID3D11DeviceContext_CSSetUnorderedAccessViews(context, 0, 1, &uav, NULL);
            ID3D11DeviceContext_Dispatch(context, 16, 1, 1);
        }
        if (pass == PASSES / 2)
        {
            /* DXVK does this one with a compute shader that takes the value as push constants. */
            static const UINT value[4] = {7, 7, 7, 7};
            ID3D11DeviceContext_ClearUnorderedAccessViewUint(context, uav, value);
        }
    }

    {
        D3D11_TEXTURE2D_DESC desc = target_desc;
        D3D11_MAPPED_SUBRESOURCE map;
        ID3D11Texture2D *staging;
        int changed = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
        ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
        unsigned first = *(unsigned *)map.pData;
        for (int y = 0; y < SIZE; y++)
            for (int x = 0; x < SIZE; x++)
                changed += ((unsigned *)((char *)map.pData + y * map.RowPitch))[x] != first;
        printf("first pixel %08x, pixels that differ from it: %d of %d\n", first, changed, SIZE * SIZE);
        ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    printf("drawcheck image %016llx compute %016llx\n",
           checksum((ID3D11Resource *)target, D3D11_RESOURCE_DIMENSION_TEXTURE2D, 0),
           checksum((ID3D11Resource *)structured, D3D11_RESOURCE_DIMENSION_BUFFER, 64 * 4));
    fflush(stdout);
    ExitProcess(0);
}
