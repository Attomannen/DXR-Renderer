// Average radiance of the environment map's upper (sky) hemisphere, written to
// a 1x1 RGBA32F target that DeferredRenderer reads back once per environment
// change. Lets a physical "sky luminance" in cd/m² scale any HDR sky, whatever
// units its texels happen to be authored in.
TextureCube gEnvironment : register(t0);
SamplerState gSampler : register(s0);
RWTexture2D<float4> gAverage : register(u0);

static const uint kSampleCount = 512u;

[numthreads(1, 1, 1)]
void main()
{
	uint width, height, mipCount;
	gEnvironment.GetDimensions(0, width, height, mipCount);
	// Mirrors EvaluateEnvironmentLighting: the first of the three
	// cosine-convolved mips is smooth enough that a few hundred samples
	// integrate it without aliasing the sun.
	const float mip = max(0.0f, float(mipCount) - 4.0f);

	// Fibonacci spiral over the +Y hemisphere (uniform in solid angle).
	float3 sum = 0.0f;
	const float goldenAngle = 2.39996323f;
	for (uint i = 0u; i < kSampleCount; ++i)
	{
		const float y = (float(i) + 0.5f) / float(kSampleCount);
		const float r = sqrt(saturate(1.0f - y * y));
		const float phi = goldenAngle * float(i);
		sum += gEnvironment.SampleLevel(gSampler, float3(cos(phi) * r, y, sin(phi) * r), mip).rgb;
	}
	const float3 average = sum / float(kSampleCount);
	gAverage[uint2(0, 0)] = float4(average, dot(average, float3(0.2126f, 0.7152f, 0.0722f)));
}
