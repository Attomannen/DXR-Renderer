// Depth of field, pass 3 of 3: blend the half-resolution bokeh back over the
// full-resolution frame.
//
// The sharp image is kept at full resolution and only replaced where the lens
// says it should be. Blurring everything and blending would throw away detail
// in the focal plane, which is the one part of the frame that must stay sharp.
#include "PostFxCommon.hlsli"

Texture2D Sharp : register(t0);
Texture2D Blurred : register(t1);

float4 main(FsIn i) : SV_TARGET
{
	const float3 sharp = Sharp.SampleLevel(LinearClamp, i.uv, 0).rgb;
	const float4 blur = Blurred.SampleLevel(LinearClamp, i.uv, 0);

	// Bilinear upsampling of the half-resolution result is deliberate and safe
	// here: the thing being upsampled is already blurred, so the interpolation
	// cannot invent detail it does not have. It would not be safe for the CoC
	// itself, which is why the blend weight comes from the blur pass's own
	// alpha rather than from a re-derived depth.
	//
	// The blur pass already ramped this from 1 to 4 pixels of radius, so it is
	// used directly. It used to threshold a radius here, which made the blend
	// effectively binary.
	const float amount = saturate(blur.a);
	return float4(lerp(sharp, blur.rgb, amount), 1.0f);
}
