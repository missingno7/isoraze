layout(location=2) in vec4 pixelpos;
layout(location=0) out vec4 FragColor;
#ifdef GBUFFER_PASS
layout(location=1) out vec4 FragFog;
layout(location=2) out vec4 FragNormal;
#endif

//===========================================================================
//
// Main shader routine
//
//===========================================================================

void main()
{
	if (IsoEyeVisibility(pixelpos.xyz) <= 0.0) discard;
	float fogdist;
	float fogfactor;

	//
	// calculate fog factor
	//
	if (uFogEnabled == -1) 
	{
		fogdist = uLightingPos.w > 0.0 ? distance(pixelpos.xyz, uLightingPos.xyz) : pixelpos.w;
	}
	else 
	{
		fogdist = max(16.0, distance(pixelpos.xyz, uLightingPos.w > 0.0 ? uLightingPos.xyz : uCameraPos.xyz));
	}
	fogfactor = exp2 (uFogDensity * fogdist);
	FragColor = vec4(uFogColor.rgb, 1.0 - fogfactor);
#ifdef GBUFFER_PASS
	FragFog = vec4(0.0, 0.0, 0.0, 1.0);
	FragNormal = vec4(0.5, 0.5, 0.5, 1.0);
#endif
}

