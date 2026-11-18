// Surface-oriented, deterministic ejecta. No screen overlay: the impact quad is depth-tested in the scene.
float impactHash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return float(x & 0xffffffu) / 16777216.0;
}

float impactStroke(float2 p, float2 a, float2 b, float width) {
    const float2 ab = b-a;
    const float u = saturate(dot(p-a,ab)/max(dot(ab,ab),1e-6));
    const float d = length(p-lerp(a,b,u));
    const float aa = max(fwidth(d),0.001);
    return 1.0-smoothstep(width-aa,width+aa,d);
}

float2 impactProject(float3 p) {
    return float2(dot(p,g_frame.cameraRight.xyz),dot(p,g_frame.cameraUp.xyz));
}

float4 impactColor(float2 p, float4 info, float3 normal) {
    const uint aspect = uint(info.x+0.5), seed = uint(info.z*10000.0);
    const float age = saturate(info.y/K_MAGIC_IMPACT_SECONDS);
    const float size = 1.5+5.0*age, fade = pow(1.0-age,1.5);
    const float3 tangent = normalize(cross(abs(normal.y)<0.95 ? float3(0,1,0) : float3(0,0,1),normal));
    const float3 bitangent = cross(normal,tangent);
    const float3 acceleration = aspect==3 || aspect==8 ? float3(0,2.0,0) : float3(0,-3.5,0);
    float sparks = 0, glints = 0;
    [unroll] for (uint j=0; j<14; ++j) {
        const float h = impactHash(seed+j*83u), h2 = impactHash(seed+j*173u+19u);
        const float angle = (j+h*0.8)*2.399963;
        const float3 radial = tangent*cos(angle)+bitangent*sin(angle);
        const float3 velocity = radial*(1.1+2.8*h)+normal*(1.0+3.0*h2);
        const float t = age*(0.7+0.3*h2), previous = max(t-0.07,0.0);
        const float3 centreOffset = normal*age*1.4;
        const float2 head = impactProject(velocity*t+acceleration*t*t-centreOffset)/size;
        const float2 tail = impactProject(velocity*previous+acceleration*previous*previous-centreOffset)/size;
        const float width = (aspect==0 ? 0.065 : 0.025)*(0.6+h)/size;
        const float drop = impactStroke(p,tail,head,width);
        sparks += drop*(0.35+0.65*h2)*fade;
        glints += exp(-dot(p-head,p-head)/max(width*width*8.0,1e-5))*fade*0.12;
    }
    const float2 origin = -impactProject(normal)*age*1.4/size;
    const float2 q = p-origin;
    const float radius = length(q), angle = atan2(q.y,q.x);
    const float noise = latticeNoise3(float3(q*size*3.5,info.z+age*1.6));
    const float haze = exp(-radius*radius*size*size*2.2)*smoothstep(0.2,0.8,noise)*fade;
    const float core = exp(-radius*radius*size*size*28.0)*exp(-age*22.0);
    float filaments = 0;
    if (aspect==4 || aspect==2 || aspect==8) {
        const float wave = sin(angle*3.0-radius*size*9.0+age*5.0+noise*2.0);
        filaments = pow(saturate(1.0-abs(wave)*8.0),2.0)*exp(-radius*size*2.5)*fade*0.45;
    }
    float3 colour = ballColor(aspect);
    float3 hot = lerp(colour,float3(1,0.92,0.7),0.38);
    float emission = 1.4, opacity = haze*0.08+sparks*0.15;
    if (aspect==0) { emission=0.55; opacity=haze*0.45+sparks*0.7; hot=float3(0.48,0.36,0.22); }
    if (aspect==1) { emission=1.0; hot=float3(0.55,0.86,1.0); }
    if (aspect==3) { emission=1.8; hot=float3(1.0,0.32,0.055); }
    if (aspect==6) { emission=3.0; hot=float3(1,0.96,0.8); }
    if (aspect==8) { emission=0.65; opacity+=haze*0.3; hot=colour*1.4; }
    const float edge = 1.0-smoothstep(0.8,1.0,length(p));
    const float3 radiance = colour*(haze*0.16+filaments)+hot*(sparks+glints+core*0.65);
    return float4(radiance*emission*edge,saturate(opacity)*edge);
}
