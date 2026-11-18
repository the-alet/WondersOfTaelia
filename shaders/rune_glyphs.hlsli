// Invented signs assembled from ordered strokes. The same path defines their carving and drawing animation.
void runeLine(float2 p, float2 a, float2 b, float budget, inout float travelled, inout float d) {
    const float len = length(b - a);
    if (budget > travelled && len > 1e-6) {
        const float2 end = lerp(a, b, saturate((budget - travelled) / len));
        const float2 ab = end - a;
        const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-8));
        d = min(d, length(p - a - ab * t));
    }
    travelled += len;
}
void runeArc(float2 p, float2 centre, float2 radii, float start, float span, float budget,
             inout float travelled, inout float d) {
    float2 prev = centre + radii * float2(cos(start), sin(start));
    [loop] for (int i = 1; i <= 24; ++i) {
        const float angle = start + span * (float(i) / 24.0);
        const float2 q = centre + radii * float2(cos(angle), sin(angle));
        runeLine(p, prev, q, budget, travelled, d);
        prev = q;
    }
}
float runePath(uint aspect, float2 p, float budget, out float total) {
    float d = 10.0, at = 0;
    const float pi = 3.14159265;
    if (aspect == 0) { // split stone: asymmetrical facets, a central seam and three roots
        const float2 q[7] = {float2(0,0.78),float2(0.59,0.32),float2(0.48,-0.42),float2(-0.12,-0.7),
                              float2(-0.61,-0.28),float2(-0.46,0.43),float2(0,0.78)};
        [unroll] for (int j=0;j<6;++j) runeLine(p,q[j],q[j+1],budget,at,d);
        runeLine(p,q[0],float2(-0.08,0.12),budget,at,d);
        runeLine(p,float2(-0.08,0.12),q[3],budget,at,d);
        runeLine(p,q[1],float2(-0.08,0.12),budget,at,d);
        runeLine(p,q[4],float2(-0.08,0.12),budget,at,d);
        [unroll] for(int j=-1;j<=1;++j) runeLine(p,float2(j*0.25,-0.56),float2(j*0.31,-0.85),budget,at,d);
    } else if (aspect == 1) { // water: three nested, offset currents and a suspended drop
        [unroll] for(int j=0;j<3;++j) runeArc(p,float2((j-1)*0.06,0.22-j*0.27),float2(0.64-j*0.10,0.28),pi,pi,budget,at,d);
        runeLine(p,float2(0.02,0.79),float2(-0.15,0.48),budget,at,d);
        runeArc(p,float2(0.02,0.48),float2(0.17,0.15),pi,pi,budget,at,d);
        runeLine(p,float2(0.19,0.48),float2(0.02,0.79),budget,at,d);
        runeArc(p,float2(-0.43,-0.6),float2(0.12,0.12),0,1.6*pi,budget,at,d);
    } else if (aspect == 2) { // life: branching double stem, closed leaves and a crown
        runeLine(p,float2(-0.08,-0.79),float2(0.08,0.62),budget,at,d);
        runeLine(p,float2(0.06,-0.73),float2(0.18,0.35),budget,at,d);
        [unroll] for(int j=0;j<3;++j) {
            const float side = j%2==0 ? -1.0 : 1.0;
            const float y = -0.35+j*0.32;
            runeLine(p,float2(0,y),float2(side*0.46,y+0.18),budget,at,d);
            runeArc(p,float2(side*0.34,y+0.18),float2(0.25,0.12),0,2*pi,budget,at,d);
        }
        runeArc(p,float2(0.07,0.64),float2(0.2,0.14),0,2*pi,budget,at,d);
    } else if (aspect == 3) { // fire: tightening spiral and three rising tongues
        float2 prev = float2(-0.59,-0.37);
        [loop] for(int j=1;j<=38;++j) {
            const float t=float(j)/38;
            const float a=3.7+7.4*t, r=0.7-0.54*t;
            const float2 q=float2(cos(a)*r,sin(a)*r+0.05);
            runeLine(p,prev,q,budget,at,d); prev=q;
        }
        [unroll] for(int j=-1;j<=1;++j) {
            runeLine(p,float2(j*0.27,0.42),float2(j*0.31-0.07,0.70),budget,at,d);
            runeLine(p,float2(j*0.31-0.07,0.70),float2(j*0.28,0.85),budget,at,d);
        }
        runeLine(p,float2(-0.32,-0.72),float2(0.28,-0.59),budget,at,d);
    } else if (aspect == 4) { // air: interlocking open vortices, swept ribs
        runeArc(p,float2(-0.2,0.24),float2(0.43,0.36),-0.4,1.8*pi,budget,at,d);
        runeArc(p,float2(0.2,-0.23),float2(0.43,0.36),pi-0.4,1.8*pi,budget,at,d);
        runeArc(p,float2(-0.2,0.24),float2(0.18,0.14),0,1.6*pi,budget,at,d);
        runeArc(p,float2(0.2,-0.23),float2(0.18,0.14),pi,1.6*pi,budget,at,d);
        runeLine(p,float2(-0.7,-0.65),float2(0.1,-0.78),budget,at,d);
        runeLine(p,float2(-0.1,0.78),float2(0.7,0.65),budget,at,d);
    } else if (aspect == 6) { // light: an irregular lens and twelve paired rays
        runeArc(p,0,float2(0.25,0.34),0,2*pi,budget,at,d);
        runeArc(p,0,float2(0.13,0.17),0,2*pi,budget,at,d);
        [unroll] for(int j=0;j<12;++j) {
            const float a=float(j)*pi/6;
            const float2 dir=float2(cos(a),sin(a));
            runeLine(p,dir*0.43,dir*(j%2==0?0.8:0.65),budget,at,d);
            runeLine(p,dir*0.65,dir*0.57+float2(-dir.y,dir.x)*0.08,budget,at,d);
        }
    } else { // neutral/necrotic: interrupted shells, inner cleft and descending ribs
        [unroll] for(int j=0;j<3;++j) runeArc(p,0,float2(0.72-j*0.19,0.66-j*0.17),0.22+j*0.3,1.65*pi,budget,at,d);
        runeLine(p,float2(-0.16,0.62),float2(0.12,-0.72),budget,at,d);
        [unroll] for(int j=0;j<3;++j) runeLine(p,float2(-0.2+j*0.21,-0.48),float2(-0.3+j*0.32,-0.82),budget,at,d);
    }
    total=at;
    return d;
}
float glyphDist(uint aspect, float2 p) {
    float total;
    runePath(aspect,p,0,total);
    float unused;
    return runePath(aspect,p,total*saturate(g_push.magic.w),unused);
}
