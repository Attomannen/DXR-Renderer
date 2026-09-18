#include "AtmosphereCommon.hlsli"
#include "SkyAtmosphereCommon.hlsli"
#include "CloudsCommon.hlsli"

// Hero volumetric cloud raymarch: a dedicated pass, separate from the
// height-fog march in AtmosphereVolumeMain.hlsli -- fog is a short haze a
// few hundred meters from the camera, clouds are a shell kilometers up
// marched over tens of kilometers near the horizon, different enough to
// want their own step budget and early-out behaviour. Runs at reduced
// resolution (DeferredRenderer::CreateVolumeTexture's divisor, same as the
// fog volume) into a premultiplied-color + transmittance target that
// AtmosphereCompositePS.hlsl upsamples and blends in.
//
// The cloud shell is treated as flat horizontal planes (Y = base/top
// altitude), not a spherical shell like the sky-atmosphere LUTs -- at
// cloud-relevant altitudes (1-4 km) and ranges, Earth's curvature is not
// worth the extra math; the physical sky already fades things into haze
// well before curvature would matter.

Texture2D<float4> SkyViewLutForClouds : register(t0);
Texture3D<float4> CloudShapeNoise : register(t1);
Texture3D<float> CloudDetailNoise : register(t2);
// Previous frame's blended output (ping-ponged with CloudsOut by
// DeferredRendererClouds.cpp -- never the same resource as CloudsOut this
// dispatch), reprojected below and blended with this frame's fresh sample.
Texture2D<float4> CloudsHistory : register(t3);
SamplerState CloudsLinearSampler : register(s0);
SamplerState CloudsHistorySampler : register(s1);   // clamp, not wrap -- see its creation
RWTexture2D<float4> CloudsOut : register(u0);

// 24 steps was under-sampling badly enough to read as hard-edged blocky
// shadow patches rather than a smooth gradient -- each step's optical depth
// jump was large enough to be individually visible once composited to the
// screen, and averaging that pattern over many frames (temporal
// reprojection, above) doesn't fix it, since it isn't noise: the same
// under-sampled shape just gets reinforced frame after frame. Profiling
// showed the whole cloud pass was a small fraction of frame time even at
// half resolution, so there was plenty of headroom to raise this.
// 96, not 48. The residual speckle on thin margins is march variance: where a
// cloud is thin, one step decides the pixel, and neighbouring pixels jittered
// to different offsets disagree. Halving the step length halves how much of
// the result any single sample carries, which is the only way to reduce that
// variance at its source -- rendering it at higher resolution or filtering it
// afterwards only makes it sharper or blurrier, never less.
static const int kPrimarySteps = 96;
// Per meter per unit density, shared by the view march and the sun march.
// 0.015 was tuned against a 1.5 km layer. The layer is now 3.8 km, so the same
// number gave two and a half times the optical depth end to end and every
// cloud saturated to a flat opaque white with a hard edge -- no thin margins,
// nothing for light to pass through. 0.009 restores roughly the old opacity
// across a traverse while keeping the taller clouds, so cores stay solid and
// edges stay translucent, which is the difference the reference shows.
static const float kExtinction = 0.014f;
// Earth's curvature hides a 1.5 km deck at ~140 km (sqrt(2 R h)); the shell
// is marched as flat planes, so cap there instead of at 60 km, where the deck
// used to stop 1.4 degrees above the horizon and leave a bare band of sky.
static const float kMaxMarchDistance = 140000.0f;   // meters; caps near-horizon rays

// Henyey-Greenstein: probability of light scattering by angle cosTheta
// (view ray dot sun-toward direction) relative to the light. Cloud droplets
// are strongly forward-scattering, which is the physical cause of the
// "silver lining" -- clouds glowing brightly when the sun sits behind them
// from the camera's view. Beer's law + powder alone have no view/sun-angle
// dependence at all, so without this every cloud was lit identically
// regardless of where the sun actually was relative to the camera.
float HenyeyGreenstein(float cosTheta, float g)
{
	float g2 = g * g;
	return (1.0f - g2) / (4.0f * 3.14159265f * pow(max(1e-4f, 1.0f + g2 - 2.0f * g * cosTheta), 1.5f));
}

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
	uint2 size;
	CloudsOut.GetDimensions(size.x, size.y);
	if (any(tid.xy >= size)) return;

	float2 uv = (tid.xy + 0.5f) / size;
	int2 pixel = min(int2(uv * float2(FogWidth, FogHeight)), int2(FogWidth - 1, FogHeight - 1));
	float depth = FogDepth.Load(int3(pixel, 0));

	if (gCloudsEnabled == 0 || depth < 0.999999f)
	{
		CloudsOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);   // transmittance 1 = no cloud
		return;
	}

	// Ray from the camera basis (see gCloudCamRight in CloudsConstants.hlsli
	// for why not FogWorld()). Same jitter convention as the fog march.
	float2 ndc = float2((uv.x + FogJitter.x / size.x) * 2.0f - 1.0f, 1.0f - (uv.y + FogJitter.y / size.y) * 2.0f);
	float3 dir = normalize(gCloudCamForward + gCloudCamRight * (ndc.x * gCloudAspect * gCloudTanHalfFovY) + gCloudCamUp * (ndc.y * gCloudTanHalfFovY));
	// True camera position, not the sky LUT's dead-zoned gCameraHeight.
	float3 origin = FogCamera * 0.01f;

	// Spherical shell on a planet of radius gBottomRadius (the sky LUTs' own),
	// centred under the camera: the deck curves down and away toward the
	// horizon instead of stretching flat to infinity. Heights along the ray
	// use the parabolic form h(t) = h0 + t dy + t^2 (1 - dy^2) / (2 (R + h0)),
	// exact enough for the 140 km march and free of the catastrophic
	// cancellation the exact sqrt form has against R^2.
	// A deliberately SMALLER planet than the sky's, for the cloud shell only.
	//
	// On the true Earth radius the deck curves away correctly but the effect is
	// slight over the distances a cloud layer is legible at: the deck reads as
	// nearly flat and runs almost to the horizon before dropping. Shrinking the
	// radius tightens that curve, so the deck visibly bends down and away and
	// the far cloud stacks up into a band the way it does in a photograph.
	// This is an artistic exaggeration, not physics, which is why it is local
	// to the cloud march and does not touch the sky LUTs' own geometry.
	const float kCurveExaggeration = 2.5f;
	const float R = gBottomRadius / kCurveExaggeration;
	const float h0 = origin.y;
	float tEnter = 0.0f, tExit = -1.0f;
	{
		// Ray vs the sphere at altitude h1, from altitude h0, dir.y = dy:
		// t^2 + 2 t (R+h0) dy + (h0-h1)(2R+h0+h1) = 0
		const float b = (R + h0) * dir.y;
		const float cBase = (h0 - gCloudBaseAltitude) * (2.0f * R + h0 + gCloudBaseAltitude);
		const float cTop  = (h0 - gCloudTopAltitude)  * (2.0f * R + h0 + gCloudTopAltitude);
		const float dBase = b * b - cBase, dTop = b * b - cTop;
		if (h0 < gCloudBaseAltitude)
		{
			// Below the deck: enter through the base sphere's far root, leave through the top's.
			if (dBase >= 0.0f && dTop >= 0.0f) { tEnter = -b + sqrt(dBase); tExit = -b + sqrt(dTop); }
		}
		else if (h0 > gCloudTopAltitude)
		{
			// Above the deck: enter through the top sphere's near root; leave at
			// the base sphere's near root if the ray dips into it, else the top's far root.
			if (dTop >= 0.0f)
			{
				const float sTop = sqrt(dTop);
				tEnter = -b - sTop;
				if (tEnter < 0.0f) tEnter = 0.0f;
				tExit = (dBase >= 0.0f && (-b - sqrt(dBase)) > 0.0f) ? (-b - sqrt(dBase)) : (-b + sTop);
			}
		}
		else
		{
			// Inside the deck.
			tEnter = 0.0f;
			tExit = (dBase >= 0.0f && (-b - sqrt(dBase)) > 0.0f) ? (-b - sqrt(dBase)) : (dTop >= 0.0f ? -b + sqrt(dTop) : -1.0f);
		}
	}
	const float curvatureK = (1.0f - dir.y * dir.y) / (2.0f * (R + h0));

	if (tExit <= tEnter)
	{
		CloudsOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	// Near the horizon, just REACHING the cloud shell (tEnter) can already
	// exceed the march budget -- clamping tExit alone but not tEnter used to
	// hard cut from "clouds" to "none" on the very next pixel once tEnter
	// crossed kMaxMarchDistance, a sharp seam slicing across the sky. Fading
	// transmittance back to 1 over the last kFadeDistance instead turns that
	// into the same soft falloff real atmospheric haze gives a receding
	// cloud deck.
	const float kFadeDistance = 40000.0f;
	float marchFade = saturate((kMaxMarchDistance - tEnter) / kFadeDistance);
	if (marchFade <= 0.0f)
	{
		CloudsOut[tid.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}
	tExit = min(tExit, kMaxMarchDistance);

	float2 skyUv = SkyViewDirToUv(dir);
	float3 ambient = SkyViewLutForClouds.SampleLevel(CloudsLinearSampler, skyUv, 0).rgb;
	float cosSun = dot(dir, gSunDirToLight);
	// Forward lobe (the silver lining) plus a weak backward lobe (real
	// clouds still scatter some light back toward a viewer facing the sun),
	// normalised so a 90-degree scattering angle keeps the same brightness
	// this shader already had before the phase function existed, and only
	// climbs well above that approaching the sun itself. Clamped so a
	// single near-sun pixel doesn't blow out uncontrollably.
	// g0/g1/lerp match Frostbite's own fitted values (Hillaire, SIGGRAPH 2016
	// "Physically Based Sky, Atmosphere & Cloud Rendering in Frostbite").
	float phase = lerp(HenyeyGreenstein(cosSun, 0.8f), HenyeyGreenstein(cosSun, -0.5f), 0.5f);
	float phaseNorm = lerp(HenyeyGreenstein(0.0f, 0.8f), HenyeyGreenstein(0.0f, -0.5f), 0.5f);
	phase = clamp(phase / max(1e-4f, phaseNorm), 0.2f, 8.0f);

	// Exponential step growth: the same step count spends its resolution on
	// the near deck, where structure is visible, and takes coarse (mipped)
	// strides through the far deck instead of aliasing it into moire rows.
	const float kStepGrowth = 1.06f;
	const float kGrowthSum = (pow(kStepGrowth, kPrimarySteps) - 1.0f) / (kStepGrowth - 1.0f);
	float stepLength = (tExit - tEnter) / kGrowthSum;
	// Angular footprint of this pixel per metre of distance, for mip selection.
	const float pixelAngle = 2.0f * gCloudTanHalfFovY / float(size.y);
	float transmittance = 1.0f;
	float3 scattered = 0.0f;
	// Un-jittered fixed-step marching samples every pixel's ray at the same
	// phase relative to its own entry point, which on a smoothly-varying
	// density field beats against the view-angle gradient into coherent
	// concentric rings (the volumetric-rendering equivalent of Moire
	// banding) instead of an even gradient. A purely spatial (tid.xy-only)
	// hash fixes that for a STILL frame, but stays screen-locked -- as the
	// camera moves, the world sweeps under that fixed comb and the banding
	// reappears as a swimming/crawling pattern. Folding in FogJitter (the
	// same per-frame TAA sub-pixel offset AtmosphereVolumeMain.hlsli's fog
	// march already mixes into its own hash) makes the pattern change every
	// frame instead, so it's temporal noise TAA can accumulate into stable
	// grain rather than a static pattern that only hides motionlessness.
	// Interleaved gradient noise (Jimenez), not a Wang hash.
	//
	// A hash gives white noise: every pixel's offset is independent of its
	// neighbours', so within any small neighbourhood the offsets clump and
	// leave gaps. Where a cloud is thin enough that one step decides the
	// result, that clumping is the stippled edge -- neighbouring pixels land on
	// opposite sides of the margin and the speckle survives both the bilinear
	// upsample from the cloud pass's half resolution and the temporal blend,
	// because there is nothing regular in it for either to average.
	//
	// Interleaved gradient noise is ordered rather than random: across any 3x3
	// block its values are spread evenly over [0,1), so a neighbourhood of
	// pixels samples the march at evenly spaced offsets and their average is
	// close to the true integral. That is exactly what the upsample and the
	// temporal history then do for free. It costs two multiplies.
	//
	// The per-frame term keeps it from being screen-locked: a purely spatial
	// pattern is stable in a still frame but sweeps across the world as the
	// camera moves, which reads as crawling. Offsetting the lattice each frame
	// makes it temporal noise the history can converge instead.
	const float frameOffset = frac(gTime * 61.0f) * 64.0f;
	const float2 jitterPos = float2(tid.xy) + 5.588238f * frameOffset;
	float jitter = frac(52.9829189f * frac(dot(jitterPos, float2(0.06711056f, 0.00583715f))));
	// Jitter is applied per step, as a fraction of THAT step: offsetting only
	// the first step leaves every later (longer) step unjittered, and the
	// sample positions then line up on iso-distance shells that read as
	// slices across the deck when it is seen from above.
	float t = tEnter;
	[loop] for (int i = 0; i < kPrimarySteps; ++i)
	{
		const float ts = t + jitter * stepLength;
		float3 samplePos = origin + dir * ts;
		const float sampleAltitude = h0 + ts * dir.y + ts * ts * curvatureK;
		// Pixel footprint only. Driving the mip from the step length as well
		// read the volume at ~190 m texels through a 1.5 km layer, which from
		// above showed as horizontal slabs.
		float footprint = (t + 0.5f * stepLength) * pixelAngle;
		float density = SampleCloudDensity(CloudShapeNoise, CloudDetailNoise, CloudsLinearSampler, samplePos, false, footprint, sampleAltitude);
		if (density > 0.001f)
		{
			float sunOpticalDepth = 0.0f;
			// March toward the sun only as far as the shell's top: the old
			// fixed shell-thickness stride from a sample near the top ran
			// most of its steps above the clouds, sampling nothing.
			float toTop = gSunDirToLight.y > 0.02f ? (gCloudTopAltitude - sampleAltitude) / gSunDirToLight.y : (gCloudTopAltitude - gCloudBaseAltitude);
			float lightStepLen = clamp(toTop, 50.0f, 4.0f * (gCloudTopAltitude - gCloudBaseAltitude)) / max(1u, gCloudLightSteps);
			[loop] for (uint l = 0; l < gCloudLightSteps; ++l)
			{
				float3 lightPos = samplePos + gSunDirToLight * ((l + 0.5f) * lightStepLen);
				// Same footprint as the view sample: mipping the sun march by its
				// own 250 m stride read the volume at mip 2, and every self-shadow
				// became a rectangle (the "boxy clouds when the sun moves" bug).
				sunOpticalDepth += SampleCloudDensity(CloudShapeNoise, CloudDetailNoise, CloudsLinearSampler, lightPos, true, footprint, sampleAltitude + (l + 0.5f) * lightStepLen * gSunDirToLight.y) * lightStepLen;
			}
			// Multi-scattering approximation (Wrenninge, "Oz: The Great and
			// Volumetric", SIGGRAPH 2010; adopted by Frostbite for clouds --
			// Hillaire, SIGGRAPH 2016): a real multi-bounce integrator is far
			// too expensive here, so instead evaluate the same single-
			// scattering term several times with extinction, phase strength
			// and contribution each halved per "octave" and sum them. This is
			// what gives thick cloud cores their soft, glowing look instead
			// of a harsh silhouette -- without it, only the first bounce of
			// light off the sun-facing surface ever shows.
			float3 sunLit = 0.0f;
			float msAtten = 1.0f, msOd = sunOpticalDepth, msPhaseMix = 0.0f;
			[unroll] for (int o = 0; o < 3; ++o)
			{
				float sunTransmittance = exp(-msOd * kExtinction);
				// Schneider's "powder" term (Horizon Zero Dawn, GDC 2015): boosts
				// apparent brightness at thin/edge density so clouds don't look
				// like flat grey cutouts facing the viewer.
				float powder = 1.0f - exp(-density * 2.0f * msAtten);
				float octavePhase = lerp(phase, 1.0f, msPhaseMix);
				sunLit += msAtten * sunTransmittance * octavePhase * (0.6f + 0.4f * powder);
				msAtten *= 0.5f; msOd *= 0.5f; msPhaseMix = saturate(msPhaseMix + 0.5f);
			}
			// Ambient: the shell's underside sees far less sky than its top.
			// gSunIlluminance is deliberately NOT attenuated by elevation -- the
			// sky LUTs need the raw value because they apply their own
			// transmittance, which is what correctly zeroes the sun once the
			// path to it passes through the planet. The cloud march has no such
			// check, so without this the deck stayed lit brilliant white with
			// the sun 30 degrees below the horizon while the ground under it
			// was dark.
			const float sunAboveHorizon = smoothstep(-0.04f, 0.04f, gSunDirToLight.y);
			float3 litColor = gSunIlluminance * sunAboveHorizon * sunLit + ambient * lerp(0.35f, 1.0f, saturate((sampleAltitude - gCloudBaseAltitude) / max(1.0f, gCloudTopAltitude - gCloudBaseAltitude)));

			float segExtinction = density * stepLength * kExtinction;
			float segTransmittance = exp(-segExtinction);
			scattered += transmittance * (1.0f - segTransmittance) * litColor;
			transmittance *= segTransmittance;
			if (transmittance < 0.01f) break;
		}
		t += stepLength;
		// Grow, but never beyond 250 m: kilometre steps through the far deck
		// read as rings where the sampled mip jumps.
		stepLength = min(stepLength * kStepGrowth, max(150.0f, (tExit - tEnter) / kPrimarySteps));
	}

	// Aerial perspective: a deck tens of kilometres away sits behind that much
	// atmosphere, so its light is scattered out and replaced by sky colour.
	// Without this the far deck kept full contrast right up to the march cap
	// and ended in a hard bright band at the horizon.
	const float kAerialDistance = 50000.0f;   // meters, ~1/e of the deck's contrast
	float aerial = exp(-max(0.0f, tEnter) / kAerialDistance);
	float opacity = (1.0f - transmittance) * marchFade * aerial;
	float3 freshColor = lerp(ambient * opacity, scattered * marchFade, aerial);
	float freshTransmittance = 1.0f - opacity;

	// Temporal reprojection (Schneider/HZD and Hillaire/Frostbite both single
	// this out as the change that actually makes a hero cloud raymarch
	// affordable): reproject last frame's blended result and mix it in
	// rather than recomputing the full march from scratch every frame.
	// Reprojected by VIEW DIRECTION, not world position -- the cloud shell
	// sits kilometers up, so a single frame's camera translation is
	// negligible parallax next to that distance; only camera rotation
	// actually moves a cloud pixel frame to frame, and a pure direction
	// (w=0) through the previous view-projection matrix captures exactly
	// that, the same trick DxrLightingCS.hlsl uses for its own "sky is
	// infinitely distant" reprojection.
	float3 blendedColor = freshColor;
	float blendedTransmittance = freshTransmittance;
	if (gCloudHistoryValid != 0)
	{
		float4 prevClip = mul(float4(dir, 0.0f), gCloudPrevWorldToClip);
		if (prevClip.w > 0.0001f)
		{
			float2 prevUv = (prevClip.xy / prevClip.w) * float2(0.5f, -0.5f) + 0.5f;
			if (all(prevUv >= 0.0f) && all(prevUv <= 1.0f))
			{
				float4 history = CloudsHistory.SampleLevel(CloudsHistorySampler, prevUv, 0);
				// The fresh sample is a single jittered march, i.e. noisy by
				// design; the history is the average of many. The previous
				// version REJECTED history whenever the two disagreed, which
				// with a noisy fresh sample happens at random per pixel per
				// frame -- so the effective history length varied from pixel
				// to pixel and the result read as blotches that crawled.
				// Instead keep the history but CLAMP it toward the fresh
				// sample with a tolerance wide enough to swallow the march
				// noise: a genuinely different view (a disocclusion, a cloud
				// that scrolled in) still pulls the history along within a
				// few frames, while noise is averaged out.
				const float kTransmittanceTolerance = 0.35f;
				history.a = clamp(history.a, freshTransmittance - kTransmittanceTolerance, freshTransmittance + kTransmittanceTolerance);
				const float3 kLum = float3(0.2126f, 0.7152f, 0.0722f);
				const float freshLum = dot(freshColor, kLum) + 1e-4f;
				const float histLum = dot(history.rgb, kLum) + 1e-4f;
				history.rgb *= clamp(freshLum / histLum, 0.25f, 4.0f);
				const float kHistoryWeight = 0.92f;
				blendedColor = lerp(freshColor, history.rgb, kHistoryWeight);
				blendedTransmittance = lerp(freshTransmittance, history.a, kHistoryWeight);
			}
		}
	}

	CloudsOut[tid.xy] = float4(blendedColor, blendedTransmittance);
}
