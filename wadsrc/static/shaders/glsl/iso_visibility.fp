#if __VERSION__ >= 330
// Lossless float transport in a nearest-filtered BGRA8 data texture.
float IsoData(int index)
{
    int width = int(uVisibilityParams.y);
    vec4 c = texelFetch(IsoVisibility, ivec2(index % width, index / width), 0).bgra;
    uvec4 b = uvec4(c * 255.0 + 0.5);
    return uintBitsToFloat(b.x | (b.y << 8u) | (b.z << 16u) | (b.w << 24u));
}
vec4 IsoData4(int index)
{
    return vec4(IsoData(index), IsoData(index+1), IsoData(index+2), IsoData(index+3));
}
float IsoCross(vec2 a, vec2 b) { return a.x*b.y-a.y*b.x; }
float IsoPlane(int sector, int plane, vec2 pos)
{
    int p = sector*12+2+plane*3;
    return dot(vec2(IsoData(p), IsoData(p+1)), pos)+IsoData(p+2);
}
bool IsoMaskBlocks(int index, vec3 pos)
{
    if (index < 0) return false;
    vec4 line = IsoData4(index);
    vec2 delta = line.zw-line.xy;
    float f = clamp(dot(pos.xz-line.xy,delta)/max(dot(delta,delta),0.00001),0.0,1.0);
    vec4 heights = IsoData4(index+4);
    float top = mix(heights.x,heights.y,f), bottom = mix(heights.z,heights.w,f);
    if (pos.y < bottom || pos.y > top) return false;
    vec4 uv = IsoData4(index+8);
    vec2 low = vec2(IsoData(index+12),IsoData(index+13));
    float heightFraction=clamp((pos.y-bottom)/max(top-bottom,0.00001),0.0,1.0);
    vec2 bottomU=vec2(IsoData(index+17),IsoData(index+18));
    vec2 tc = vec2(mix(mix(bottomU.x,bottomU.y,f),mix(uv.x,uv.y,f),heightFraction),
        mix(mix(low.x,low.y,f),mix(uv.z,uv.w,f),heightFraction));
    int alpha = int(IsoData(index+14));
    int w=int(IsoData(alpha)), h=int(IsoData(alpha+1));
    // Wall sprites clamp their artwork; ordinary masked walls repeat it.
    if (IsoData(index+15)>0.5) tc=clamp(tc,vec2(0.0),vec2(0.999999));
    else tc=fract(tc);
    ivec2 pixel=ivec2(tc*vec2(w,h));
    return IsoData(alpha+2+pixel.y*w+pixel.x)>0.5;
}
// Horizontal scenery asks whether any vertical opening exists at this
// column. Low decorative sprites do not hide entire rooms or buildings.
bool IsoHorizontalMaskBlocks(int index, vec2 pos, float floor, float ceiling)
{
    if(index<0) return false;
    vec4 line=IsoData4(index);
    vec2 d=line.zw-line.xy;
    float f=clamp(dot(pos-line.xy,d)/max(dot(d,d),0.00001),0.0,1.0);
    vec4 z=IsoData4(index+4);
    if(mix(z.z,z.w,f)>floor+0.05 || mix(z.x,z.y,f)<ceiling-0.05) return false;
    bool rotated=IsoData(index+16)>0.5;
    float u=rotated ? mix(IsoData(index+10),IsoData(index+11),f) : mix(IsoData(index+8),IsoData(index+9),f);
    u=IsoData(index+15)>0.5?clamp(u,0.0,0.999999):fract(u);
    int alpha=int(IsoData(index+14));
    int w=int(IsoData(alpha)),h=int(IsoData(alpha+1));
    return IsoData(alpha+2+w*h+(rotated?w+int(u*float(h)):int(u*float(w))))>0.5;
}
float IsoEyeVisibility(vec3 target)
{
    if (uVisibilityOrigin.w<0.5 || uVisibilityTarget.z>3.5) return 1.0;
    bool horizontal=uVisibilityParams.w>0.5 && uVisibilityTarget.z<1.5;
    vec3 eye=uVisibilityOrigin.xyz;
    vec3 ray=target-eye;
    float length3=max(length(ray),0.001);
    float epsilon=min(0.0001,0.04/length3);
    int sector=int(uVisibilityOrigin.w)-1;
    float start=0.0;
    float edge=1e20;
    for(int step=0;step<96;++step)
    {
        int p=sector*12;
        int first=int(IsoData(p)), count=int(IsoData(p+1));
        float nearest=1.0;
        float fraction=0.0;
        int crossed=-1;
        for(int i=0;i<count;++i)
        {
            int w=first+i*8;
            vec4 line=IsoData4(w);
            vec2 d=line.zw-line.xy;
            float den=IsoCross(ray.xz,d);
            if(abs(den)<0.000001) continue;
            vec2 from=line.xy-eye.xz;
            float t=IsoCross(from,d)/den;
            float u=IsoCross(from,ray.xz)/den;
            if(t>start+epsilon && t<nearest && u>=-0.000001 && u<=1.000001)
            { nearest=t; fraction=clamp(u,0.0,1.0); crossed=w; }
        }
        // Solid-wall silhouette endpoints bound visibility even when the
        // corner lies inside one concave sector. Collinear wall subdivisions
        // are excluded by the CPU's cap flags.
        float xyLength=length(ray.xz);
        if(uVisibilityParams.x>0.0 && xyLength>0.001)
        for(int i=0;i<count;++i)
        {
            int w=first+i*8;
            if(IsoData(w+4)>=0.0) continue;
            vec4 line=IsoData4(w);
            for(int end=0;end<2;++end)
            {
                if(IsoData(w+6+end)<0.5) continue;
                vec2 ep=end==0?line.xy:line.zw;
                float t=dot(ep-eye.xz,ray.xz)/(xyLength*xyLength);
                if(t<=start+epsilon || t>=nearest-epsilon) continue;
                float gap=abs(IsoCross(ep-eye.xz,ray.xz))/xyLength;
                edge=min(edge,gap/max(t,0.001));
            }
        }
        vec3 last=eye+ray*max(start,nearest-epsilon);
        float floor=IsoPlane(sector,0,last.xz), ceiling=IsoPlane(sector,1,last.xz);
        // Both physical planes remain occluders, including omitted ceilings.
        if(!horizontal && (last.y<floor-0.08 || last.y>ceiling+0.08)) return 0.0;
        // Opaque portions of wall-aligned sprites, in this sector only.
        int sprites=int(IsoData(p+8)), spriteCount=int(IsoData(p+9));
        for(int i=0;i<spriteCount;++i)
        {
            int m=int(IsoData(sprites+i));
            vec4 line=IsoData4(m);
            vec2 d=line.zw-line.xy;
            float den=IsoCross(ray.xz,d);
            if(abs(den)<0.000001) continue;
            float t=IsoCross(line.xy-eye.xz,d)/den;
            float f=IsoCross(line.xy-eye.xz,ray.xz)/den;
            if(t>start+epsilon && t<nearest-epsilon && f>=0.0 && f<=1.0)
            {
                vec3 at=eye+ray*t;
                bool blocks=horizontal ? IsoHorizontalMaskBlocks(m,at.xz,IsoPlane(sector,0,at.xz),IsoPlane(sector,1,at.xz)) : IsoMaskBlocks(m,at);
                if(blocks) return 0.0;
            }
        }
        if(nearest>=1.0-epsilon || crossed<0)
        {
            bool owns = uVisibilityTarget.z>2.5 || sector==int(uVisibilityTarget.x) ||
                (uVisibilityTarget.z>0.5 && uVisibilityTarget.z<1.5 && sector==int(uVisibilityTarget.y));
            if(!owns) return 0.0;
            return uVisibilityParams.x>0.0 ? smoothstep(0.0,uVisibilityParams.x,edge) : 1.0;
        }
        vec3 hit=eye+ray*nearest;
        int next=int(IsoData(crossed+4));
        if(next<0) return 0.0;
        float nf=IsoPlane(next,0,hit.xz), nc=IsoPlane(next,1,hit.xz);
        if(!horizontal && (hit.y<=nf+0.001 || hit.y>=nc-0.001)) return 0.0;
        float sf=IsoPlane(sector,0,hit.xz), sc=IsoPlane(sector,1,hit.xz);
        float low=max(sf,nf), high=min(sc,nc);
        // Height still determines whether a door is shut. It does not slice
        // the presentation of an otherwise visible floor or wall.
        if(horizontal && high<=low+0.001) return 0.0;
        int mask=int(IsoData(crossed+5));
        if(horizontal ? IsoHorizontalMaskBlocks(mask,hit.xz,low,high) : IsoMaskBlocks(mask,hit)) return 0.0;
        float scale=1.0/max(nearest,0.001);
        if(!horizontal && nf>sf+0.01) edge=min(edge,(hit.y-nf)*scale);
        if(!horizontal && nc<sc-0.01) edge=min(edge,(nc-hit.y)*scale);
        vec4 line=IsoData4(crossed);
        float width=length(line.zw-line.xy)*scale;
        if(IsoData(crossed+6)>0.5) edge=min(edge,fraction*width);
        if(IsoData(crossed+7)>0.5) edge=min(edge,(1.0-fraction)*width);
        start=nearest;
        sector=next;
    }
    // Invalid cycles or excessive portal depth fail closed, never reveal a room.
    return 0.0;
}

#else
// FPS remains available on ES 2; the experimental eye mask requires GLSL 3.30.
float IsoEyeVisibility(vec3 target) { return uVisibilityOrigin.w > 0.5 && uVisibilityTarget.z < 3.5 ? 0.0 : 1.0; }
#endif
