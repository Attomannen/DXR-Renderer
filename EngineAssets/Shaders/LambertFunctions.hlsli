#ifndef LAMBERT_FUNCTIONS_HLSLI
#define LAMBERT_FUNCTIONS_HLSLI

#define nMipOffset 3

float3 EvaluateAmbianceLambert(TextureCube lysBurleyCube, float3 vN, float3 dfcol)
{
    int numMips = GetNumMips(lysBurleyCube);
    const int nrBrdMips = numMips - nMipOffset;
    float3 diffRad = lysBurleyCube.SampleLevel(defaultSampler, vN, (float) (nrBrdMips - 1)).xyz;
    return dfcol * diffRad;
}

float3 EvaluateSoftDirectionalLightLambert(float3 albedoColor, float3 normal, float softness, float3 lightColor, float3 lightDir)
{
    // wrap lighting, with normalization so that total amount of light is constant roughly over whole range and exactly at softness 0, 0.5 and 1
    float lambert = max(0.f, lerp(dot(normal, lightDir), 1.f, softness)) * lerp(1.f, 0.5f, softness) / lerp(1.f, 2.0f, softness);

    return lightColor * lambert * albedoColor;
}

float3 EvaluateDirectionalLightLambert(float3 albedoColor, float3 normal, float3 lightColor, float3 lightDir)
{
    float lambert = saturate(dot(normal, lightDir));    
    return lightColor * lambert * albedoColor;
}

float3 EvaluatePointLightLambert(float3 albedoColor, float3 normal, float3 lightColor, float lightRange, float3 lightPos, float3 pixelPos)
{
    // Compute som useful values
    float3 lightDir = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(lightDir); // centimeter to meter
    lightDir = normalize(lightDir);
	
    float lambert = saturate(dot(normal, lightDir));
      
    float rangeFactor = lightDistance / (0.01f * lightRange);
    rangeFactor *= rangeFactor;
    rangeFactor *= rangeFactor;

    float rangleLimit = saturate(1.f - rangeFactor);
    rangleLimit *= rangleLimit;

    float finalAttenuation = lambert * rangleLimit / (lightDistance * lightDistance + 0.001f);

    return lightColor * finalAttenuation * albedoColor;
}

float3 EvaluateSoftAreaLightLambert(float3 albedoColor, float3 normal,
    float3 lightColor, float lightRadius, float lightRange, float3 lightPos, float3 pixelPos)
{
    // Compute som useful values
    float3 lightDir = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(lightDir);
    lightDir = normalize(lightDir);

    float NdL;
    float lambertAttentuation;

    float radiusMeter = 0.01f * lightRadius;
    float rangeMeter = 0.01f * lightRange;

    // Approximation of normal distribution area light, derived by Bjorn Ottosson
    // instead of taking dot product between normal and light and dividing by square distance
    // we approximate as a mix between the dot product and a constant (sometimes called wrapped shading), witha blend varying based on distance
    // the two different curves are a result of numerical optimization
    // and blends between light from all directions in the middle of the light and normal fallof far away 
    // the curves for the blending also give the modified attenuation 

    {
        float cosA = dot(normal, lightDir);

        float a = 0.54f;
        float b = 1.60f;

        float ra = radiusMeter * a;
        float ra4 = ra * ra * ra * ra;
        float x4 = lightDistance * lightDistance * lightDistance * lightDistance;

        float combinedAttenuation = rsqrt(ra4 + x4);
        float constantAttenuation = sqrt(radiusMeter) * rsqrt(radiusMeter * ra4 + b * lightDistance * x4);

        float cosAttentuation = combinedAttenuation - constantAttenuation;

        lambertAttentuation = max(0, cosA * cosAttentuation + constantAttenuation);

        // scale NdH closer to center of area light to avoid artifacts inside the light
        float k = constantAttenuation / combinedAttenuation;
    }

    
    float rangeFactor = lightDistance / rangeMeter;
    rangeFactor *= rangeFactor;
    rangeFactor *= rangeFactor;

    float rangleLimit = saturate(1.f - rangeFactor);
    rangleLimit *= rangleLimit;

    float attenuation = rangleLimit * lambertAttentuation;

    return lightColor * attenuation * albedoColor;
}

float3 EvaluateSpotLightLambert(float3 albedoColor, float3 normal,
    float3 lightColor, float lightIntensity, float lightRange,
    float3 lightPos, float3 lightDir, float outerAngle, float innerAngle, float3 pixelPos)
{
    float3 toLight = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(toLight); // centimeter to meterz
    toLight = normalize(toLight);

    float NdL = saturate(dot(normal, toLight));
    float lambert = NdL; // Angle attenuation
    
    float cosOuterAngle = cos(outerAngle);
    float cosInnerAngle = cos(innerAngle);
    float3 lightDirection = lightDir;

    // Determine if pixel is within cone.
    float theta = dot(toLight, normalize(-lightDirection));
	// And if we're in the inner or outer radius.
    float epsilon = cosInnerAngle - cosOuterAngle;
    float intensity = clamp((theta - cosOuterAngle) / epsilon, 0.0f, 1.0f);
    intensity *= intensity;
	
    float rangeFactor = lightDistance / (0.01f * lightRange);
    rangeFactor *= rangeFactor;
    rangeFactor *= rangeFactor;

    float rangleLimit = saturate(1.f - rangeFactor);
    rangleLimit *= rangleLimit;

    float finalAttenuation = lambert * intensity * rangleLimit / (lightDistance * lightDistance + 0.001f);

    return lightColor * lightIntensity * finalAttenuation * albedoColor;
}

#endif // LAMBERT_FUNCTIONS_HLSLI