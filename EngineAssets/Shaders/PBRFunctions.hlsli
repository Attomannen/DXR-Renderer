#ifndef PBRFUNCTIONS_HLSLI
#define PBRFUNCTIONS_HLSLI


//// BRDF Globals
#define FLT_EPSILON 1.192092896e-07f
#define nMipOffset 3
#define PI 3.14159265358979323846f

#define ENABLE_ENERGY_CONSERVATION
  
// Old lighting implementation
// kept for a while for reference while developing 
//#define USE_OLD_LIGHTING
 
#ifdef USE_OLD_LIGHTING

float bias(float value, float b)
{
    return (b > 0.0) ? pow(abs(value), log(b) / log(0.5)) : 0.0f;
}

float gain(float value, float g)
{
    return 0.5 * ((value < 0.5) ? bias(2.0 * value, 1.0 - g) : (2.0 - bias(2.0 - 2.0 * value, 1.0 - g)));
}

float RoughnessFromPerceptualRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

float PerceptualRougnessFromRoughness(float roughness)
{
    return sqrt(max(0.0, roughness));
}

float SpecularPowerFromPerceptualRoughness(float perceptualRoughness)
{
    float roughness = RoughnessFromPerceptualRoughness(perceptualRoughness);
    return (2.0 / max(FLT_EPSILON, roughness * roughness)) - 2.0;
}

float PerceptualRougnessFromSpecularPower(float specularPower)
{
    float roughness = sqrt(2.0 / (specularPower + 2.0));
    return PerceptualRougnessFromRoughness(roughness);
}

float BurleyToMip(float fPerceptualRoughness, int nMips, float NdotR)
{
    float specPower = SpecularPowerFromPerceptualRoughness(fPerceptualRoughness);
    specPower /= (4 * max(NdotR, FLT_EPSILON));
    float scale = PerceptualRougnessFromSpecularPower(specPower);
    return scale * (nMips - 1 - nMipOffset);
}

float3 GetSpecularDominantDir(float3 vN, float3 vR, float roughness)
{
    float invRough = saturate(1 - roughness);
    float alpha = invRough * (sqrt(invRough) + roughness);

    return lerp(vN, vR, alpha);
}

float GetReductionInMicrofacets(float perceptualRoughness)
{
    float roughness = RoughnessFromPerceptualRoughness(perceptualRoughness);

    return 1.0 / (roughness * roughness + 1.0);
}

float EmpiricalSpecularAO(float ao, float perceptualRoughness)
{
    float smooth = 1 - perceptualRoughness;
    float specAO = gain(ao, 0.5 + max(0.0, smooth * 0.4));

    return min(1.0, specAO + lerp(0.0, 0.5, smooth * smooth * smooth * smooth));
}

float ApproximateSpecularSelfOcclusion(float3 vR, float3 vertNormalNormalized)
{
    const float fadeParam = 1.3;
    float rimmask = clamp(1 + fadeParam * dot(vR, vertNormalNormalized), 0.0, 1.0);
    rimmask *= rimmask;

    return rimmask;
}

float3 Diffuse(float3 pAlbedo)
{
    return pAlbedo / PI;
}

float NormalDistribution_GGX(float a, float NdH)
{
    // Isotropic ggx
    float a2 = a * a;
    float NdH2 = NdH * NdH;

    float denominator = NdH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}

float Geometric_Smith_Schlick_GGX(float a, float NdV, float NdL)
{
    // Smith Schlick-GGX
    float k = a * 0.5f;
    float GV = NdV / (NdV * (1 - k) + k);
    float GL = NdL / (NdL * (1 - k) + k);

    return GV * GL;
}

float3 Fresnel_Schlick(float3 specularColor, float3 h, float3 v)
{
    return (specularColor + (1.0f - specularColor) * pow((1.0f - saturate(dot(v, h))), 5));
}

float3 Specular(float3 specularColor, float3 h, float3 v, float a, float NdL, float NdV, float NdH)
{
    return ((NormalDistribution_GGX(a, NdH) * Geometric_Smith_Schlick_GGX(a, NdV, NdL)) * Fresnel_Schlick(specularColor, h, v)) / (4.0f * NdL * NdV + 0.0001f);
}

float3 EvaluateAmbiance(TextureCube lysBurleyCube, float3 vN, float3 VNUnit, float3 toEye, float perceptualRoughness, float ao, float3 dfcol, float3 spccol)
{
    int numMips = GetNumMips(lysBurleyCube);
    const int nrBrdMips = numMips - nMipOffset;
    float VdotN = saturate(dot(toEye, vN)); //clamp(dot(toEye, vN), 0.0, 1.0f);
    const float3 vRorg = 2 * vN * VdotN - toEye;

    float3 vR = GetSpecularDominantDir(vN, vRorg, RoughnessFromPerceptualRoughness(perceptualRoughness));
    float RdotNsat = saturate(dot(vN, vR));

    float mipLevel = BurleyToMip(perceptualRoughness, numMips, RdotNsat);

    float3 specRad = lysBurleyCube.SampleLevel(defaultSampler, vR, mipLevel).xyz;
    float3 diffRad = lysBurleyCube.SampleLevel(defaultSampler, vN, (float) (nrBrdMips - 1)).xyz;

    float fT = 1.0 - RdotNsat;
    float fT5 = fT * fT;
    fT5 = fT5 * fT5 * fT;
    spccol = lerp(spccol, (float3) 1.0, fT5);

    float fFade = GetReductionInMicrofacets(perceptualRoughness);
    fFade *= EmpiricalSpecularAO(ao, perceptualRoughness);
    fFade *= ApproximateSpecularSelfOcclusion(vR, VNUnit);

    float3 ambientdiffuse = ao * dfcol * diffRad;
    float3 ambientspecular = fFade * spccol * specRad;

    return ambientdiffuse + ambientspecular;
}

float3 EvaluateDirectionalLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness, float3 lightColor, float3 lightDir, float3 viewDir)
{
    // Compute som useful values
    float NdL = saturate(dot(normal, lightDir));
    float lambert = NdL; // Angle attenuation
    float NdV = saturate(dot(normal, viewDir));
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float VdH = saturate(dot(viewDir, h));
    float LdV = saturate(dot(lightDir, viewDir));
    float a = max(0.001f, roughness * roughness);

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, a, NdL, NdV, NdH);

    return lightColor * lambert * (cDiff * (1.0 - cSpec) + cSpec) * PI;
}

float3 EvaluateSoftDirectionalLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness, float softness, float3 lightColor, float3 lightDir, float3 viewDir)
{
    // not implemented in the old path
    return EvaluateDirectionalLight(albedoColor, specularColor, normal, roughness, lightColor, lightDir, viewDir);
}

float3 EvaluatePointLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness,
    float3 lightColor, float lightRange, float3 lightPos, float3 viewDir, float3 pixelPos)
{
    // Compute som useful values
    float3 lightDir = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f*length(lightDir);
    lightDir = normalize(lightDir);
	
    float NdL = saturate(dot(normal, lightDir));
    float lambert = NdL; // Angle attenuation
    float NdV = saturate(dot(normal, viewDir));
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float a = max(0.001f, roughness * roughness);

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, a, NdL, NdV, NdH);

    float ue4Attenuation = ((pow(saturate(1 - pow(lightDistance / (0.01f * lightRange), 4.0f)), 2.0f)) / (pow(lightDistance, 2.0f) + 0.01f)); // Unreal Engine 4 attenuation
    float attenuation = ue4Attenuation * lambert;

    return lightColor * attenuation * ((cDiff + cSpec) * PI);
}

float3 EvaluateSoftAreaLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness,
    float3 lightColor, float lightRadius, float lightRange, float3 lightPos, float3 viewDir, float3 pixelPos)
{
    return EvaluatePointLight(albedoColor, specularColor, normal, roughness, lightColor, lightRange, lightPos, viewDir, pixelPos);
}

float3 EvaluateSpotLight(float3 albedoColor, float3 specularColor, float3 normal,
    float roughness, float3 lightColor, float lightRange,
    float3 lightPos, float3 lightDir, float outerAngle, float innerAngle, float3 viewDir, float3 pixelPos)
{
    float3 toLight = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(toLight);
    toLight = normalize(toLight);

    float NdL = saturate(dot(normal, toLight));
    float lambert = NdL; // Angle attenuation
    float NdV = saturate(dot(normal, viewDir));
    float3 h = normalize(toLight + viewDir);
    float NdH = saturate(dot(normal, h));
    float a = max(0.001f, roughness * roughness);

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, a, NdL, NdV, NdH);

    float cosOuterAngle = cos(outerAngle);
    float cosInnerAngle = cos(innerAngle);
    float3 lightDirection = lightDir;

    // Determine if pixel is within cone.
    float theta = dot(toLight, normalize(-lightDirection));
	// And if we're in the inner or outer radius.
    float epsilon = cosInnerAngle - cosOuterAngle;
    float intensity = clamp((theta - cosOuterAngle) / epsilon, 0.0f, 1.0f);
    intensity *= intensity;
	
    float ue4Attenuation = ((pow(saturate(1 - pow(lightDistance / (0.01f * lightRange), 4.0f)), 2.0f)) / (pow(lightDistance, 2.0f) + 0.01f)); // Unreal Engine 4 attenuation
    float finalAttenuation = lambert * intensity * ue4Attenuation;

    return lightColor * finalAttenuation * ((cDiff + cSpec) * PI);
}



#else

// Based on Practical Realtime Strategies for Accurated Indirect Occlusion, GTAO 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
float3 GTAOMultiBounce(float v, float3 albedo)
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;

    return max(v, ((v * a + b) * v + c) * v);
}

// Based on approximation described here: https://www.unrealengine.com/en-US/blog/physically-based-shading-on-mobile
// Used for environment specular color and energy conservations
float2 BrdfSpecularColorScaleOffset(float roughness, float NoV)
{
    const float4 c0 = { -1, -0.0275, -0.572, 0.022 };
    const half4 c1 = { 1, 0.0425, 1.04, -0.04 };
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return AB;
}

float2 CalculateSpecularAndDiffuseEnergyConservation(float roughness, float NdV, float3 specularColor)
{
    // Multiscattering approximation. 
    // Method used in multiple places, for example: 
    // "The Road toward Unified Rendering with Unity�s High Definition Render Pipeline": https://advances.realtimerendering.com/s2018/Siggraph%202018%20HDRP%20talk_with%20notes.pdf
    float2 ab = BrdfSpecularColorScaleOffset(roughness, NdV);
    const float specularLuma = dot(specularColor, float3(0.2126f, 0.7152f, 0.0722f));
    float specScale = 1.f + specularLuma * max(0.f, 1.f / (ab.x + ab.y) - 1.f);
    
    // Remove amount of light handled by specular
    // Based on: https://c0de517e.blogspot.com/2019/08/misunderstanding-multilayering-diffuse.html
    float diffScale = max(0.f, 1.f - (ab.x * specularLuma + ab.y) * specScale);
    
    return float2(specScale, diffScale);
}

// Disney principle brdf roughness remapping, since then industry standard
float RoughnessFromPerceptualRoughness(float perceptualRoughness)
{
    return perceptualRoughness * perceptualRoughness;
}

float PerceptualRougnessFromRoughness(float roughness)
{
    return sqrt(max(0.0, roughness));
}

float BurleyToMip(float fPerceptualRoughness, int nMips)
{
    // Compute mip level from roughness, this math comes from Lys and matches how it generates mips when set to "Burley"
    float fScale = fPerceptualRoughness * (1.7 - 0.7 * fPerceptualRoughness);
    return fScale * (nMips - 1 - nMipOffset);
}

// from "Moving Frostbite to PBR":
// https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
float3 GetSpecularDominantDir(float3 vN, float3 vR, float roughness)
{
    float invRough = saturate(1 - roughness);
    float alpha = invRough * (sqrt(invRough) + roughness);

    return lerp(vN, vR, alpha);
}

float bias(float value, float b)
{
    return (b > 0.0) ? pow(value, log(b) / log(0.5)) : 0.0;
}
float gain(float value, float g)
{
    return 0.5 * ((value < 0.5) ? bias(2.0 * value, 1.0 - g) : (2.0 - bias(2.0 - 2.0 * value, 1.0 - g)));
}

float EmpiricalSpecularAO(float ao, float perceptualRoughness)
{
    // from Lys example code
    // basically a ramp curve allowing ao on very diffuse specular
    // and gradually less so as the reflection hardens.
    // seems way to expensive though, with the pow and everything...
    float smooth = 1 - perceptualRoughness;
    float specAO = gain(ao, 0.5 + max(0.0, smooth * 0.4));

    return min(1.0, specAO + lerp(0.0, 0.5, smooth * smooth * smooth * smooth));
}

// marmoset horizon occlusion http://marmosetco.tumblr.com/post/81245981087
float ApproximateSpecularSelfOcclusion(float3 vR, float3 vertNormalNormalized)
{
    const float fadeParam = 1.3;
    float rimmask = clamp(1 + fadeParam * dot(vR, vertNormalNormalized), 0.0, 1.0);
    rimmask *= rimmask;

    return rimmask;
}

float3 Diffuse(float3 pAlbedo)
{
    return pAlbedo / PI;
}

float NormalDistribution_GGX(float a, float NdH)
{
    // Isotropic ggx
    float a2 = a * a;
    float NdH2 = NdH * NdH;

    float denominator = NdH2 * (a2 - 1.0f) + 1.0f;
    denominator *= denominator;
    denominator *= PI;

    return a2 / denominator;
}

float Geometric_Smith_Schlick_GGX(float a, float NdV, float NdL)
{
    // Smith Schlick-GGX
    float k = a * 0.5f;
    float GV = NdV / (NdV * (1 - k) + k);
    float GL = NdL / (NdL * (1 - k) + k);

    return GV * GL;
}

float3 Fresnel_Schlick(float3 specularColor, float3 h, float3 v)
{
    float k = 1.0f - saturate(dot(v, h));
    return (specularColor + (1.0f - specularColor) * k * k * k * k * k);
}

float3 Specular(float3 specularColor, float3 h, float3 v, float a, float NdL, float NdV, float NdH)
{
    return ((NormalDistribution_GGX(a, NdH) * Geometric_Smith_Schlick_GGX(a, NdV, NdL)) * Fresnel_Schlick(specularColor, h, v)) / (4.0f * NdL * NdV + 0.0001f);
}

// Box-projected reflection: intersect the reflection ray with the probe's AABB
// and re-aim it at the hit point so the cubemap sample lines up with nearby walls.
float3 BoxParallaxCorrect(float3 vR, float3 worldPos, float3 boxCenter, float3 boxHalf)
{
    float3 fromCenter = worldPos - boxCenter;
    float3 inside = boxHalf - abs(fromCenter);
    if (any(inside <= 0.0f)) return vR;   // outside the influence box -> no correction

    // Keep the direction off exact zero so the slab test can't divide by zero on
    // axis-aligned reflections (a flat wall facing an axis).
    float3 rd = max(abs(vR), 1e-4f) * sign(vR + 1e-9f);
    float3 invR = 1.0f / rd;

    float3 tMax = max((boxHalf - fromCenter) * invR, (-boxHalf - fromCenter) * invR);
    float tHit = min(min(tMax.x, tMax.y), tMax.z);
    if (tHit <= 0.0f) return vR;

    float3 corrected = normalize(worldPos + vR * tHit - boxCenter);
    // Fade to the uncorrected direction near the box walls -- the projection is
    // unstable there and produces swirl artifacts on surfaces sitting on a face.
    float edge = saturate(min(inside.x, min(inside.y, inside.z)) / (max(max(boxHalf.x, boxHalf.y), boxHalf.z) * 0.15f));
    return normalize(lerp(vR, corrected, edge));
}

float3 EvaluateAmbiance(TextureCube lysBurleyCube, float3 vN, float3 VNUnit, float3 toEye, float perceptualRoughness, float ao, float3 dfcol, float3 spccol, float3 worldPos, out float3 oAmbientSpecular)
{
    int numMips = GetNumMips(lysBurleyCube);
    const int nrBrdMips = numMips - nMipOffset;

    float NdV = abs(dot(vN, toEye)) + 1e-5f;

    const float3 vRorg = 2 * vN * NdV - toEye;

    float3 vR = GetSpecularDominantDir(vN, vRorg, RoughnessFromPerceptualRoughness(perceptualRoughness));
    // Box-project the reflection ray so it lines up with nearby walls. Fade the
    // correction out for near-mirror surfaces -- the low-res probe can't serve a
    // sharp reflection and the projection swirls; SSR covers those instead.
    [branch] if (gProbeBoxCenter.w > 0.5f)
    {
        float pxAmt = saturate((perceptualRoughness - 0.08f) * 6.0f);
        if (pxAmt > 0.0f)
            vR = normalize(lerp(vR, BoxParallaxCorrect(vR, worldPos, gProbeBoxCenter.xyz, gProbeBoxHalf.xyz), pxAmt));
    }
    float mipLevel = BurleyToMip(perceptualRoughness, numMips);

    float3 specRad = lysBurleyCube.SampleLevel(defaultSampler, vR, mipLevel).xyz;
    float3 diffRad = lysBurleyCube.SampleLevel(defaultSampler, vN, (float)(nrBrdMips - 1)).xyz;

    float2 ab = gBrdfLutTexture.SampleLevel(defaultSampler, float2(NdV, perceptualRoughness), 0).rg;

    float3 specularColor = spccol * ab.x + ab.y;
    
    #ifdef ENABLE_ENERGY_CONSERVATION
    specularColor = specularColor + specularColor * spccol * (1.f / (ab.x + ab.y) - 1.f);
    #endif

    float specAO = EmpiricalSpecularAO(ao, perceptualRoughness);
    float selfOcclusion = ApproximateSpecularSelfOcclusion(vR, VNUnit);
    float specVisibility = selfOcclusion * specAO;

    // combined color, as if looking straight at the materials
    // used as a substitute for bounced light
    float3 combinedColor = dfcol * (1.f - spccol) + spccol;
    
    // Compensates for energy lost due to now having full local GI solution
    // makes the AO a bit weaker and colored by the diffuse reflection color
    float3 aoWithBounce = GTAOMultiBounce(ao, combinedColor);
    
    float3 ambientdiffuse = aoWithBounce * dfcol * diffRad;
    #ifdef ENABLE_ENERGY_CONSERVATION
    ambientdiffuse *= max(0.f, 1.f - specularColor);
    #endif
    
    // when occluded, behave as if a second ray hit the material itself
    // incorrect, but better approximation than black  
    #ifdef ENABLE_ENERGY_CONSERVATION
    float3 occludedColor = aoWithBounce * combinedColor * diffRad;
    float3 ambientspecular = specularColor * lerp(occludedColor, specRad, specVisibility);
    #else
    float3 ambientspecular = specularColor * specRad * specVisibility;
    #endif

    oAmbientSpecular = ambientspecular;
	return ambientdiffuse + ambientspecular;
}

float3 EvaluateSoftDirectionalLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness, float softness, float3 lightColor, float3 lightDir, float3 viewDir)
{
    // wrap lighting, with normalization so that total amount of light is constant roughly over whole range and exactly at softness 0, 0.5 and 1
    float NdL = max(0.f, lerp(dot(normal, lightDir), 1.f, softness)) * lerp(1.f, 0.5f, softness) / lerp(1.f, 2.0f, softness);
    float lambert = NdL; 

    // negative NdV values are wrong, but occur due to normal mapping
    // taking abs here is for example what is done here: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
    float NdV = abs(dot(normal, viewDir)) + 1e-5f;
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float VdH = saturate(dot(viewDir, h));
    float LdV = saturate(dot(lightDir, viewDir));

    float alpha = max(0.001f, roughness * roughness);
    
    // specular D modification for increasing the specular size, to roughly match the softness of the directional light
    // From "Real Shading in Unreal Engine 4", but adapted to directional light
    alpha = min(1.0, alpha + softness);
    
    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, alpha, NdL, NdV, NdH);

#ifdef ENABLE_ENERGY_CONSERVATION
    float2 specDiffScale = CalculateSpecularAndDiffuseEnergyConservation(roughness, NdV, specularColor);
    cSpec *= specDiffScale.x;
    cDiff *= specDiffScale.y;
#endif
    
    return lightColor * lambert * (cDiff + cSpec) * PI;
}

float3 EvaluateDirectionalLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness, float3 lightColor, float3 lightDir, float3 viewDir)
{
    // Compute som useful values
    float NdL = saturate(dot(normal, lightDir));
    float lambert = NdL; // Angle attenuation
    
    // negative NdV values are wrong, but occur due to normal mapping
    // taking abs here is for example what is done here: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
    float NdV = abs(dot(normal, viewDir)) + 1e-5f;
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float VdH = saturate(dot(viewDir, h));
    float LdV = saturate(dot(lightDir, viewDir));
    
    float alpha = max(0.001f, roughness * roughness);
    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, alpha, NdL, NdV, NdH);
    
#ifdef ENABLE_ENERGY_CONSERVATION
    float2 specDiffScale = CalculateSpecularAndDiffuseEnergyConservation(roughness, NdV, specularColor);
    cSpec *= specDiffScale.x;
    cDiff *= specDiffScale.y;
#endif
    
    return lightColor * lambert * (cDiff + cSpec) * PI;
}

float3 EvaluatePointLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness,
    float3 lightColor, float lightRange, float3 lightPos, float3 viewDir, float3 pixelPos)
{
    // Compute som useful values
    float3 lightDir = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(lightDir); // centimeter to meter
    lightDir = normalize(lightDir);
	
    float NdL = saturate(dot(normal, lightDir));
    float lambert = NdL; // Angle attenuation
    
    // negative NdV values are wrong, but occur due to normal mapping
    // taking abs here is for example what is done here: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
    float NdV = abs(dot(normal, viewDir)) + 1e-5f;
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));
    float a = max(0.001f, roughness * roughness);

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, a, NdL, NdV, NdH);

#ifdef ENABLE_ENERGY_CONSERVATION
    float2 specDiffScale = CalculateSpecularAndDiffuseEnergyConservation(roughness, NdV, specularColor);
    cSpec *= specDiffScale.x;
    cDiff *= specDiffScale.y;
#endif
    
    float rangeFactor = lightDistance / (0.01f * lightRange);
    rangeFactor *= rangeFactor;
    rangeFactor *= rangeFactor;

    float rangleLimit = saturate(1.f - rangeFactor);
    rangleLimit *= rangleLimit;

    float finalAttenuation = lambert * rangleLimit / (lightDistance * lightDistance + 0.001f);

    return lightColor * finalAttenuation * ((cDiff + cSpec) * PI);
}

float3 EvaluateSoftAreaLight(float3 albedoColor, float3 specularColor, float3 normal, float roughness,
    float3 lightColor, float lightRadius, float lightRange, float3 lightPos, float3 viewDir, float3 pixelPos)
{
    // Compute som useful values
    float3 lightDir = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f*length(lightDir);
    lightDir = normalize(lightDir);

    float NdL;
    float lambertAttentuation;
    float3 h = normalize(lightDir + viewDir);
    float NdH = saturate(dot(normal, h));

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
        NdH = lerp(NdH, 0.f, k * k * k * k );
        
        // normalize based on combined attentuation, to get 0-1 range,and something roughly matching NdL
        // unclear how accurate it is to plug this into the specular calculation
        // but should atleast remove the light direction dependence close to the light
        NdL = lambertAttentuation / combinedAttenuation;
    }

    // negative NdV values are wrong, but occur due to normal mapping
    // taking abs here is for example what is done here: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
    float NdV = abs(dot(normal, viewDir)) + 1e-5f;
    
    float alpha = max(0.001f, roughness * roughness);

    // specular D modification for increasing the specular size, to roughly match the area light
    // From "Real Shading in Unreal Engine 4"
    alpha = min(1.0, alpha + radiusMeter / (3.f * lightDistance));

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, alpha, NdL, NdV, NdH);

#ifdef ENABLE_ENERGY_CONSERVATION
    float2 specDiffScale = CalculateSpecularAndDiffuseEnergyConservation(roughness, NdV, specularColor);
    cSpec *= specDiffScale.x;
    cDiff *= specDiffScale.y;
#endif
    
    float rangeFactor = lightDistance / rangeMeter;
    rangeFactor *= rangeFactor;
    rangeFactor *= rangeFactor;

    float rangleLimit = saturate(1.f - rangeFactor);
    rangleLimit *= rangleLimit;

    float attenuation = rangleLimit * lambertAttentuation;

    return lightColor * attenuation * (cDiff + cSpec) * PI;
}

float3 EvaluateSpotLight(float3 albedoColor, float3 specularColor, float3 normal,
    float roughness, float3 lightColor, float lightRange,
    float3 lightPos, float3 lightDir, float outerAngle, float innerAngle, float3 viewDir, float3 pixelPos)
{
    float3 toLight = lightPos.xyz - pixelPos.xyz;
    float lightDistance = 0.01f * length(toLight); // centimeter to meterz
    toLight = normalize(toLight);

    float NdL = saturate(dot(normal, toLight));
    float lambert = NdL; // Angle attenuation
    
    // negative NdV values are wrong, but occur due to normal mapping
    // taking abs here is for example what is done here: https://seblagarde.wordpress.com/wp-content/uploads/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf
    float NdV = abs(dot(normal, viewDir)) + 1e-5f;
    float3 h = normalize(toLight + viewDir);
    float NdH = saturate(dot(normal, h));
    float a = max(0.001f, roughness * roughness);

    float3 cDiff = Diffuse(albedoColor);
    float3 cSpec = Specular(specularColor, h, viewDir, a, NdL, NdV, NdH);

#ifdef ENABLE_ENERGY_CONSERVATION
    float2 specDiffScale = CalculateSpecularAndDiffuseEnergyConservation(roughness, NdV, specularColor);
    cSpec *= specDiffScale.x;
    cDiff *= specDiffScale.y;
#endif
    
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

    return lightColor * finalAttenuation * ((cDiff + cSpec) * PI);
}

#endif

#endif
