#ifndef CUBEMAP_COMMON_HLSLI
#define CUBEMAP_COMMON_HLSLI

#define PI 3.14159265358979323846f

// Radical inverse (Van der Corput sequence) in base 2 for quasi-Monte Carlo integration
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

// 2D Hammersley point set in [0, 1)^2: low-discrepancy quasi-Monte Carlo sequence
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// Unit direction vector corresponding to a cubemap face and (u, v) in [-1, 1]
// Standard DirectX Cubemap coordinate convention (+X, -X, +Y, -Y, +Z, -Z)
float3 GetCubeDirection(uint face, float2 uv)
{
    float3 dir = float3(0.0f, 0.0f, 0.0f);
    switch (face)
    {
    case 0: dir = float3(1.0f, -uv.y, -uv.x); break; // +X (Right)
    case 1: dir = float3(-1.0f, -uv.y, uv.x); break; // -X (Left)
    case 2: dir = float3(uv.x, 1.0f, uv.y);   break; // +Y (Top)
    case 3: dir = float3(uv.x, -1.0f, -uv.y); break; // -Y (Bottom)
    case 4: dir = float3(uv.x, -uv.y, 1.0f);  break; // +Z (Front)
    case 5: dir = float3(-uv.x, -uv.y, -1.0f);break; // -Z (Back)
    }
    return normalize(dir);
}

#endif // CUBEMAP_COMMON_HLSLI
