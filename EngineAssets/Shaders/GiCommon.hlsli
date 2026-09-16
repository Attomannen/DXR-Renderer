// Shared helpers for the Stage-1 emissive-GI irradiance volume.
// Probes store L2 spherical harmonics (9 RGB coeffs) of incoming radiance;
// reconstruction folds the cosine lobe so the result is diffuse irradiance / PI.
#ifndef GI_COMMON_HLSLI
#define GI_COMMON_HLSLI

// L2 real SH basis for a unit direction.
void ShBasis(float3 d, out float b[9])
{
	b[0] = 0.282095f;
	b[1] = 0.488603f * d.y;
	b[2] = 0.488603f * d.z;
	b[3] = 0.488603f * d.x;
	b[4] = 1.092548f * d.x * d.y;
	b[5] = 1.092548f * d.y * d.z;
	b[6] = 0.315392f * (3.0f * d.z * d.z - 1.0f);
	b[7] = 1.092548f * d.x * d.z;
	b[8] = 0.546274f * (d.x * d.x - d.y * d.y);
}

// Un-normalised direction for cubemap face f at face-uv in [-1,1].
// 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
float3 CubeFaceDir(uint f, float2 uv)
{
	if (f == 0) return float3( 1.0f, -uv.y, -uv.x);
	if (f == 1) return float3(-1.0f, -uv.y,  uv.x);
	if (f == 2) return float3( uv.x,  1.0f,  uv.y);
	if (f == 3) return float3( uv.x, -1.0f, -uv.y);
	if (f == 4) return float3( uv.x, -uv.y,  1.0f);
	return              float3(-uv.x, -uv.y, -1.0f);
}

// Diffuse irradiance (already / PI) from an SH set, evaluated along normal n.
// Ahat = cosine-lobe zonal factors with the PI folded out: {1, 2/3, 1/4}.
float3 EvalGiSH(float3 sh[9], float3 n)
{
	float b[9];
	ShBasis(n, b);
	float3 e = sh[0] * b[0];
	e += (sh[1] * b[1] + sh[2] * b[2] + sh[3] * b[3]) * (2.0f / 3.0f);
	e += (sh[4] * b[4] + sh[5] * b[5] + sh[6] * b[6] + sh[7] * b[7] + sh[8] * b[8]) * (1.0f / 4.0f);
	return max(e, 0.0f);
}

#endif
