// Re-applies NRD's denoised indirect diffuse and specular onto the DXR lighting.
// DxrLightingCS left that term out of gOutput and wrote it demodulated
// (divided by max(diffuse albedo * texture AO, 0.01)) into gNrdDiffuse, and
// the specular term divided by its split-sum albedo into gNrdSpecular.
Texture2D<float4> DenoisedDiffuse : register(t0);
Texture2D<float4> DiffuseAlbedo : register(t1); // rgb = diffuse albedo, a = texture AO (0 for sky)
Texture2D<float4> DenoisedSpecular : register(t2);
Texture2D<float4> SpecularAlbedo : register(t3); // rgb = specular demodulator (already clamped)
Texture2D<float4> Validation : register(t4);       // NRD debug overlay (RGBA8, alpha = coverage)
RWTexture2D<float4> Lighting : register(u0);

cbuffer NrdCompositeCB : register(b0)
{
	uint gReblur;        // outputs are YCoCg (REBLUR_BackEnd_UnpackRadianceAndNormHitDist)
	uint gValidation;    // replace the image with NRD's validation overlay
	float gOverlayScale; // scene units that display as white (inverse exposure)
	uint _pad;
};

float3 YCoCgToLinear(float3 c)
{
	const float t = c.x - c.z;
	return max(float3(t + c.y, c.x + c.z, t - c.y), 0.0f);
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
	uint width, height;
	Lighting.GetDimensions(width, height);
	if (dtid.x >= width || dtid.y >= height)
		return;

	const float4 albedo = DiffuseAlbedo[dtid.xy];
	if (gValidation != 0u)
	{
		const float4 overlay = Validation[dtid.xy];
		const float3 base = Lighting[dtid.xy].rgb;
		Lighting[dtid.xy] = float4(lerp(base, pow(overlay.rgb, 2.2f) * gOverlayScale, overlay.a), 1.0f);
		return;
	}

	float3 diffuse = DenoisedDiffuse[dtid.xy].rgb;
	float3 specular = DenoisedSpecular[dtid.xy].rgb;
	if (gReblur != 0u)
	{
		diffuse = YCoCgToLinear(diffuse);
		specular = YCoCgToLinear(specular);
	}
	diffuse = all(isfinite(diffuse)) ? max(diffuse, 0.0f) : 0.0f;
	specular = all(isfinite(specular)) ? max(specular, 0.0f) : 0.0f;

	// Sky pixels have zero alpha and a zero specular albedo; NRD leaves their
	// outputs undefined, so neither term may reach them.
	const float3 diffuseLit = albedo.a > 0.0f ? diffuse * max(albedo.rgb * albedo.a, 0.01f) : 0.0f;
	const float3 lit = diffuseLit + specular * SpecularAlbedo[dtid.xy].rgb;
	Lighting[dtid.xy] = float4(Lighting[dtid.xy].rgb + lit, Lighting[dtid.xy].a);
}
