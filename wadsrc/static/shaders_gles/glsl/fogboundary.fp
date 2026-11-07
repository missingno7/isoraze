varying vec4 pixelpos;

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
#if (DEF_FOG_ENABLED == 1) && (DEF_FOG_RADIAL == 0) && (DEF_FOG_COLOURED == 1) // This was uFogEnabled = -1,, TODO check this
	{
		fogdist = uLightingPos.w > 0.0 ? distance(pixelpos.xyz, uLightingPos.xyz) : pixelpos.w;
	}
#else
	{
		fogdist = max(16.0, distance(pixelpos.xyz, uLightingPos.w > 0.0 ? uLightingPos.xyz : uCameraPos.xyz));
	}
#endif
	fogfactor = exp2 (uFogDensity * fogdist);

	gl_FragColor = vec4(uFogColor.rgb, 1.0 - fogfactor);
}

