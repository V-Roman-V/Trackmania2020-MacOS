/* Compiles the stand-in vertex shaders in src/d3d11/d3d11_gs_emulation.h (patches/d9vk/0003) into .dxbc files.
 * usage: standin-shaders.exe <path to d3dcompiler_47.dll> */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <d3dcompiler.h>

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *,
                                     const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static const char coarse_ao[] =
    "cbuffer DrawG : register(b0) {\n"
    "  float4 f4Jitter;\n"
    "  float2 f2Offset;\n"
    "  uint uSliceIndex;\n"
    "  float padding;\n"
    "};\n"
    "struct Out {\n"
    "  float4 position : SV_Position;\n"
    "  float2 texcoord : TEXCOORD0;\n"
    "  uint layer : SV_RenderTargetArrayIndex;\n"
    "};\n"
    "Out main(uint id : SV_VertexID) {\n"
    "  Out o;\n"
    "  float2 uv = float2((id << 1) & 2, id & 2);\n"
    "  o.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);\n"
    "  o.texcoord = uv;\n"
    "  o.layer = uSliceIndex;\n"
    "  return o;\n"
    "}\n";

static const char full_triangle_batch[] =
    "cbuffer ShaderG : register(b0) {\n"
    "  uint iSliceStart;\n"
    "  float3 Padding;\n"
    "};\n"
    "struct Out {\n"
    "  float4 position : SV_Position;\n"
    "  uint layer : SV_RenderTargetArrayIndex;\n"
    "};\n"
    "Out main(uint id : SV_VertexID) {\n"
    "  Out o;\n"
    "  uint corner = id % 3;\n"
    "  o.position = float4(corner == 2 ? 3.03 : -1.01, corner == 1 ? 3.03 : -1.01, 0.0, 1.0);\n"
    "  o.layer = id / 3 + iSliceStart;\n"
    "  return o;\n"
    "}\n";

static void build(compile_fn compile, const char *src, const char *path)
{
    ID3DBlob *code = NULL, *errors = NULL;
    FILE *f;

    if (FAILED(compile(src, strlen(src), path, NULL, NULL, "main", "vs_5_0", 0, 0, &code, &errors)))
    {
        printf("%s failed: %s\n", path, errors ? (char *)errors->lpVtbl->GetBufferPointer(errors) : "?");
        return;
    }
    f = fopen(path, "wb");
    fwrite(code->lpVtbl->GetBufferPointer(code), 1, code->lpVtbl->GetBufferSize(code), f);
    fclose(f);
    printf("%s %lu bytes\n", path, (unsigned long)code->lpVtbl->GetBufferSize(code));
}

int main(int argc, char **argv)
{
    compile_fn compile = (compile_fn)GetProcAddress(LoadLibraryA(argv[1]), "D3DCompile");

    if (!compile)
        return 1;
    build(compile, coarse_ao, "vs_coarse_ao.dxbc");
    build(compile, full_triangle_batch, "vs_full_triangle_batch.dxbc");
    fflush(stdout);
    ExitProcess(0);
}
