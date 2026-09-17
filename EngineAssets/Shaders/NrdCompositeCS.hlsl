// Re-applies NRD's denoised indirect diffuse onto the DXR lighting.
// DxrSmokeTestCS left that term out of gOutput and wrote it demodulated
// (divided by max(diffuse albedo * texture AO, 0.01)) into gNrdDiffuse.
Texture2D<float4> DenoisedDiffuse : register(t0);
Texture2D<float4> DiffuseAlbedo : register(t1); // rgb = diffuse albedo, a = texture AO (0 for sky)
RWTexture2D<float4> Lighting : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint width, height;
	Lighting.GetDimensions(width, height);
	if (dtid.x >= width || dtid.y >= height)
		return;

	const float4 albedo = DiffuseAlbedo[dtid.xy];
	if (albedo.a <= 0.0f)
		return;

	const float3 radiance = DenoisedDiffuse[dtid.xy].rgb;
	if (!all(isfinite(radiance)))
		return;

	const float3 modulator = max(albedo.rgb * albedo.a, 0.01f);
	Lighting[dtid.xy] = float4(Lighting[dtid.xy].rgb + max(radiance, 0.0f) * modulator, Lighting[dtid.xy].a);
}
