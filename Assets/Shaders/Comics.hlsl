//@entry ExecutePS
// ----------------------------------------------------------------------------
// Stylized “ink & hatch” comics shader (GI-aware, moderated posterize + robust sky/sun artifact fixes).
// BOOSTED VERSION: Intensified outlines, posterization, hatching, tint & style blending.
// ----------------------------------------------------------------------------

#define COMICS_HAVE_RESOURCES 1
#define COMICS_FORCE_FALLBACK 0

// Global intensity scalar (tune one knob to scale all boosts)
#ifndef COMICS_STYLE_BOOST
#define COMICS_STYLE_BOOST 0.75
#endif

// Feature toggles
#define COMICS_ENABLE_HALFTONE      0   // Still off (no implementation section below)
#define COMICS_ENABLE_OUTLINES      1
#define COMICS_ENABLE_POSTERIZE     1
#define COMICS_ENABLE_HATCHING      1
#define COMICS_ENABLE_GI_AWARE      1
#define COMICS_ENABLE_POSTER_DITHER 1

// Optional: define if engine uses reversed depth (1 at near, 0 at far)
// #define COMICS_REVERSED_DEPTH 1

cbuffer ComicsCB : register(b0)
{
    float4 Params0; // x=EdgeDepthScale, y=EdgeLumaScale, z=EdgeThreshold, w=PosterizeLevels
    float4 Params1; // x=HalftoneScale, y=OutlineStrength, z=OutlineWidthPx, w=ColorSaturation
    float4 Params2; // x=ScreenWidth, y=ScreenHeight, z=TintStrength, w=StyleBlend
    float4 TintColor; // xyz tint, w = StrokeIntensity
}

/*
	Default parameter fallbacks (BOOSTED):
	Original -> Boosted
	EdgeThreshold: 0.85 -> 0.60 (more edges)
	PosterizeLevels: 6.0 -> 4.0 (stronger quantization)
	OutlineStrength: 0.35 -> 0.55 (darker lines)
	OutlineWidthPx: 1.1 -> 1.35 (slightly thicker)
	StrokeIntensity: 0.55 -> 0.85 (deeper hatching)
	ColorSaturation: 1.0 -> 1.15 (slightly richer)
	TintStrength default 0 (leave, controlled externally)
	StyleBlend curve scaling increased further in code
*/

#ifndef BINDLESS_CB0
#define EDGE_DEPTH_SCALE    (Params0.x > 0 ? Params0.x : (1.15 * COMICS_STYLE_BOOST))
#define EDGE_LUMA_SCALE     (Params0.y > 0 ? Params0.y : (1.20 * COMICS_STYLE_BOOST))
#define EDGE_THRESHOLD      (Params0.z > 0 ? Params0.z : 0.60)            // lower => more edges
#define POSTERIZE_LEVELS    (Params0.w > 0 ? Params0.w : 4.0)             // fewer levels => stronger effect

#define HALFTONE_SCALE      (Params1.x > 0 ? Params1.x : 160.0)           // untouched (feature off)
#define OUTLINE_STRENGTH    (Params1.y > 0 ? Params1.y : (0.55 * COMICS_STYLE_BOOST))
#define OUTLINE_WIDTH_PX    (Params1.z > 0 ? Params1.z : (1.35))
#define COLOR_SATURATION    (Params1.w > 0 ? Params1.w : (1.15 * COMICS_STYLE_BOOST))

#define SCREEN_SIZE         (Params2.xy)
#define TINT_STRENGTH       (Params2.z)
#define STYLE_BLEND         (Params2.w)

#define STROKE_INTENSITY    (TintColor.w > 0 ? TintColor.w : (0.85 * COMICS_STYLE_BOOST))
#endif

// ---- Outline visibility helpers ----
#ifndef EDGE_POWER
#define EDGE_POWER 0.65      // slightly lower -> thicker perceived mid edges
#endif
#ifndef OUTLINE_MIN_VISIBILITY
#define OUTLINE_MIN_VISIBILITY 0.14
#endif
#ifndef OUTLINE_DILATE_NEIGHBORS
#define OUTLINE_DILATE_NEIGHBORS 1
#endif

// ---- SKY HANDLING FIXES ----
#ifndef SKY_DEPTH_CUTOFF
#define SKY_DEPTH_CUTOFF 0.99905
#endif
#ifndef SKY_SOFT_WIDTH
#define SKY_SOFT_WIDTH 0.0012
#endif
#define PURE_SKY_DISABLE_STYLING 1
#define PURE_SKY_EDGE_SUPPRESS   1
// ---------------------------------------------------------------------------

#if COMICS_HAVE_RESOURCES
Texture2D SceneColor : register(t0);
Texture2D SceneDepth : register(t1);
#if COMICS_ENABLE_GI_AWARE
Texture2D SVOGIIndirect : register(t2);
#endif
SamplerState LinearClamp : register(s0);
SamplerState PointClamp : register(s1);
#endif

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float3 LumaWeights = float3(0.299, 0.587, 0.114);
float Luma(float3 c)
{
    return dot(c, LumaWeights);
}

float3 AdjustSaturation(float3 c, float sat)
{
    float g = Luma(c);
    return lerp(float3(g, g, g), c, sat);
}

float3 PosterizeLuma(float3 c, float levels)
{
    float l = Luma(c);
    float n = max(2.0, levels);
    float q = floor(l * n + 0.5) / n;
    float3 chroma = c / max(l, 1e-4);
    return saturate(chroma * q);
}

float3 PosterizeRGB(float3 c, float levels)
{
    float n = max(0.5, levels);
    return floor(c * n + 0.5) / n;
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float FetchDepth(float2 uv)
{
#if COMICS_HAVE_RESOURCES
    return SceneDepth.SampleLevel(PointClamp, uv, 0).r;
#else
    return 0.5;
#endif
}

float PureSkyMaskFromDepth(float d)
{
#ifdef COMICS_REVERSED_DEPTH
    float m = smoothstep(SKY_SOFT_WIDTH, 0.0, d);
#else
    float m = smoothstep(SKY_DEPTH_CUTOFF - SKY_SOFT_WIDTH, SKY_DEPTH_CUTOFF, d);
#endif
    return m;
}

float ComputeSkySunMask(float2 uv, float3 color)
{
#if !COMICS_HAVE_RESOURCES
    return 0.0;
#else
    uint w, h;
    SceneColor.GetDimensions(w, h);
    if (w == 0 || h == 0)
    {
        w = (uint) ((SCREEN_SIZE.x > 0) ? SCREEN_SIZE.x : 1920);
        h = (uint) ((SCREEN_SIZE.y > 0) ? SCREEN_SIZE.y : 1080);
    }
    float2 texel = 1.0 / float2(w, h);

    float dC = FetchDepth(uv);
    float dX1 = FetchDepth(uv + float2(texel.x, 0));
    float dX2 = FetchDepth(uv - float2(texel.x, 0));
    float dY1 = FetchDepth(uv + float2(0, texel.y));
    float dY2 = FetchDepth(uv - float2(0, texel.y));
    float v = (abs(dX1 - dX2) + abs(dY1 - dY2)) * 0.5;

#ifdef COMICS_REVERSED_DEPTH
    float farMask = smoothstep(0.004, 0.0005, dC);
#else
    float farMask = smoothstep(0.995, 0.9995, dC);
#endif

    float varMask = 1.0 - saturate(v * 9000.0);
    float lum = max(color.r, max(color.g, color.b));
    float highlight = smoothstep(0.78, 0.92, lum);
    float sunCoreBoost = highlight * saturate(varMask + 0.25);

    float skyMask = farMask * varMask;
    float skySun = saturate(max(skyMask, sunCoreBoost));
    return skySun;
#endif
}

float LinePattern(float2 uv, float scale, float thickness)
{
    float2 p = uv * scale;
    float l = frac(p.x + p.y * 0.02);
    float d = abs(l - 0.5);
    return smoothstep(thickness, thickness * 0.4, d);
}

float RotHatch(float2 uv, float scale, float angle, float thickness)
{
    float s = sin(angle), c = cos(angle);
    float2 r = float2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    return LinePattern(r, scale, thickness);
}

float HatchFactor(float2 uv, float shade)
{
#if !COMICS_ENABLE_HATCHING
    return 1.0;
#else
    float inv = 1.0 - shade;
    if (inv <= 0.015) // a little more permissive
        return 1.0;

    // Denser / slightly finer pattern mix
    float h1 = RotHatch(uv, 170.0, 0.35, 0.44);
    float h2 = RotHatch(uv, 170.0, -0.85, 0.44);
    float h3 = RotHatch(uv, 130.0, 1.30, 0.37);
    float h4 = RotHatch(uv, 100.0, -1.10, 0.35);
    float h5 = RotHatch(uv, 85.0, 0.72, 0.33); // extra layer for darkest tones

    float f = 1.0;
    if (inv > 0.22)
        f = min(f, h1);
    if (inv > 0.40)
        f = min(f, h2);
    if (inv > 0.58)
        f = min(f, h3);
    if (inv > 0.72)
        f = min(f, h4);
    if (inv > 0.85)
        f = min(f, h5);

    float boost = STROKE_INTENSITY * inv * 0.90 * COMICS_STYLE_BOOST;
    return lerp(1.0, f, boost);
#endif
}

// --- Edge detector ---
float EdgeFactor(float2 uv)
{
#if !COMICS_HAVE_RESOURCES || !COMICS_ENABLE_OUTLINES
    return 0;
#else
    float dCenter = FetchDepth(uv);
#ifdef COMICS_REVERSED_DEPTH
    if (PureSkyMaskFromDepth(dCenter) > 0.999) return 0;
#else
    if (PureSkyMaskFromDepth(dCenter) > 0.999)
        return 0;
#endif

    uint w, h;
    SceneColor.GetDimensions(w, h);
    if (w == 0 || h == 0)
    {
        w = (uint) ((SCREEN_SIZE.x > 0) ? SCREEN_SIZE.x : 1920);
        h = (uint) ((SCREEN_SIZE.y > 0) ? SCREEN_SIZE.y : 1080);
    }
    float2 size = float2(w, h);
    float2 texel = 1.0 / size;

#if COMICS_ENABLE_GI_AWARE
    float giL = Luma(SVOGIIndirect.SampleLevel(LinearClamp, uv, 0).rgb);
#else
    float giL = 0.0;
#endif

    const float HF_SUPPRESS_STRENGTH = 0.80;
    const float DEPTH_PREF = 1.55 * COMICS_STYLE_BOOST;
    const float WIDE_SCALE = 1.05;
    const float COLOR_EDGE_MIN_DEPTH = 0.00025;
    const float COLOR_EDGE_MAX_DEPTH = 0.0018;

    float2 o1 = texel * OUTLINE_WIDTH_PX;
    float2 o2 = texel * (OUTLINE_WIDTH_PX * 2.4);

    float3 c = SceneColor.SampleLevel(LinearClamp, uv, 0).rgb;
    float3 cxp = SceneColor.SampleLevel(LinearClamp, uv + float2(o1.x, 0), 0).rgb;
    float3 cxn = SceneColor.SampleLevel(LinearClamp, uv - float2(o1.x, 0), 0).rgb;
    float3 cyp = SceneColor.SampleLevel(LinearClamp, uv + float2(0, o1.y), 0).rgb;
    float3 cyn = SceneColor.SampleLevel(LinearClamp, uv - float2(0, o1.y), 0).rgb;
    float3 c_pp = SceneColor.SampleLevel(LinearClamp, uv + o1, 0).rgb;
    float3 c_nn = SceneColor.SampleLevel(LinearClamp, uv - o1, 0).rgb;
    float3 c_pn = SceneColor.SampleLevel(LinearClamp, uv + float2(o1.x, -o1.y), 0).rgb;
    float3 c_np = SceneColor.SampleLevel(LinearClamp, uv + float2(-o1.x, o1.y), 0).rgb;

    float l = Luma(c);
    float lxp = Luma(cxp), lxn = Luma(cxn);
    float lyp = Luma(cyp), lyn = Luma(cyn);
    float l_pp = Luma(c_pp), l_nn = Luma(c_nn);
    float l_pn = Luma(c_pn), l_np = Luma(c_np);

    float gx = lxp - lxn;
    float gy = lyp - lyn;
    float lumEdgeOrtho = (abs(gx) + abs(gy)) * 0.5;
    float lumEdgeDiag = (abs(l_pp - l_nn) + abs(l_pn - l_np)) * 0.5;
    float lumEdgeNarrow = max(lumEdgeOrtho, lumEdgeDiag);

    float3 cxp2 = SceneColor.SampleLevel(LinearClamp, uv + float2(o2.x, 0), 0).rgb;
    float3 cxn2 = SceneColor.SampleLevel(LinearClamp, uv - float2(o2.x, 0), 0).rgb;
    float3 cyp2 = SceneColor.SampleLevel(LinearClamp, uv + float2(0, o2.y), 0).rgb;
    float3 cyn2 = SceneColor.SampleLevel(LinearClamp, uv - float2(0, o2.y), 0).rgb;
    float lumEdgeWide = (abs(Luma(cxp2) - Luma(cxn2)) + abs(Luma(cyp2) - Luma(cyn2))) * 0.5;

    float hfRatio = (lumEdgeNarrow > 1e-4) ? (lumEdgeNarrow - lumEdgeWide) / lumEdgeNarrow : 0.0;
    float hfMask = saturate(hfRatio * 4.0);
    float filteredLumEdge = lumEdgeNarrow * (1.0 - hfMask * HF_SUPPRESS_STRENGTH);

    float dxp = FetchDepth(uv + float2(o1.x, 0));
    float dxn = FetchDepth(uv - float2(o1.x, 0));
    float dyp = FetchDepth(uv + float2(0, o1.y));
    float dyn = FetchDepth(uv - float2(0, o1.y));
    float dgx = dxp - dxn;
    float dgy = dyp - dyn;
    float depthEdgeOrtho = (abs(dgx) + abs(dgy));
    float d_pp = FetchDepth(uv + o1);
    float d_nn = FetchDepth(uv - o1);
    float d_pn = FetchDepth(uv + float2(o1.x, -o1.y));
    float d_np = FetchDepth(uv + float2(-o1.x, o1.y));
    float depthEdgeDiag = (abs(d_pp - d_nn) + abs(d_pn - d_np)) * 0.5;
    float depthEdgeNarrow = max(depthEdgeOrtho, depthEdgeDiag);

    float d2xp = FetchDepth(uv + float2(o2.x, 0));
    float d2xn = FetchDepth(uv - float2(o2.x, 0));
    float d2yp = FetchDepth(uv + float2(0, o2.y));
    float d2yn = FetchDepth(uv - float2(0, o2.y));
    float depthEdgeWide = abs(d2xp - d2xn) + abs(d2yp - d2yn);

    float depthSupport = smoothstep(COLOR_EDGE_MIN_DEPTH, COLOR_EDGE_MAX_DEPTH,
                                    depthEdgeNarrow + depthEdgeWide * 0.30);

    float colorStructuralFactor = saturate(depthSupport + (1.0 - hfMask) * 0.60);
    filteredLumEdge *= colorStructuralFactor;

    float edgeRaw =
        EDGE_LUMA_SCALE * (filteredLumEdge * (1.0 + lumEdgeWide * WIDE_SCALE)) +
        EDGE_DEPTH_SCALE * (depthEdgeNarrow * DEPTH_PREF + depthEdgeWide * 0.60);

    edgeRaw += giL * EDGE_LUMA_SCALE * 0.35 * COMICS_STYLE_BOOST;

    float silhouetteBoost = smoothstep(EDGE_THRESHOLD * 1.2, EDGE_THRESHOLD * 4.0, depthEdgeWide);
    edgeRaw *= (1.0 + 1.05 * silhouetteBoost * COMICS_STYLE_BOOST);

    float edge = smoothstep(EDGE_THRESHOLD, EDGE_THRESHOLD * 2.4, edgeRaw);
    return saturate(edge);
#endif
}

float3 WarmCoolGradeSoft(float3 c)
{
    float l = Luma(c);
    float3 cool = float3(0.92, 0.96, 1.04);
    float3 warm = float3(1.06, 1.00, 0.94);
    float t = saturate((l - 0.32) / 0.42);
    float3 graded = lerp(cool * c, warm * c, t);
    return lerp(c, graded, STYLE_BLEND * 0.95 * COMICS_STYLE_BOOST);
}

float3 ApplyTint(float3 c)
{
    return lerp(c, c * (TintColor.xyz * (1.0 + 0.15 * COMICS_STYLE_BOOST)), saturate(TINT_STRENGTH));
}

float4 ExecutePS(VSOut IN) : SV_Target0
{
#if !COMICS_HAVE_RESOURCES || COMICS_FORCE_FALLBACK
    return float4(0.95,0.95,0.95,1);
#else
    float3 original = SceneColor.SampleLevel(LinearClamp, IN.uv, 0).rgb;
    float depthCenter = FetchDepth(IN.uv);
    float pureSkyMask = PureSkyMaskFromDepth(depthCenter);

    float targetSat = lerp(1.0, COLOR_SATURATION, STYLE_BLEND);
    float3 styled = AdjustSaturation(original, targetSat);
    styled = WarmCoolGradeSoft(styled);
    styled = ApplyTint(styled);

    float skySunMask = ComputeSkySunMask(IN.uv, original);
    if (pureSkyMask > 0.5)
        skySunMask = 1.0;

    float sunCoreMask = 0.0;
    if (skySunMask > 0.0)
    {
        sunCoreMask = smoothstep(0.90, 0.98, max(original.r, max(original.g, original.b))) * skySunMask;
        if (pureSkyMask > 0.5)
            sunCoreMask *= 0.0;
    }

    float giMask = 0.0;
#if COMICS_ENABLE_GI_AWARE
    float3 giSample = SVOGIIndirect.SampleLevel(LinearClamp, IN.uv, 0).rgb;
    giMask = saturate(pow(Luma(giSample), 0.72) * 1.25 * COMICS_STYLE_BOOST);
#endif

#if COMICS_ENABLE_POSTERIZE
    bool allowStyle = (PURE_SKY_DISABLE_STYLING == 0) || (pureSkyMask < 0.999);

    float adaptiveLevels = POSTERIZE_LEVELS - giMask * 0.8 * COMICS_STYLE_BOOST;
    adaptiveLevels = max(2.0, adaptiveLevels);
    adaptiveLevels += (skySunMask - sunCoreMask) * 2.5;

    float3 preQuant = styled;
#if COMICS_ENABLE_POSTER_DITHER
    if (allowStyle && skySunMask > 0.0 && sunCoreMask < 0.4)
    {
        uint sw, sh;
        SceneColor.GetDimensions(sw, sh);
        if (sw == 0 || sh == 0)
        {
            sw = (uint) ((SCREEN_SIZE.x > 0) ? SCREEN_SIZE.x : 1920);
            sh = (uint) ((SCREEN_SIZE.y > 0) ? SCREEN_SIZE.y : 1080);
        }
        float2 ip = floor(IN.uv * float2(sw, sh));
        float noise = Hash12(ip);
        float dAmp = (0.50 / (adaptiveLevels + 1.0)) * skySunMask * COMICS_STYLE_BOOST;
        if (pureSkyMask > 0.5)
            dAmp *= 0.0;
        preQuant = saturate(preQuant + (noise - 0.5) * dAmp);
    }
#endif
    if (allowStyle)
    {
        float3 pRGB = PosterizeRGB(preQuant, adaptiveLevels);
        float3 pLuma = PosterizeLuma(pRGB, adaptiveLevels * 0.60);
        float3 posterized = pLuma;

        float styleCurve = smoothstep(0.0, 1.0, STYLE_BLEND);
        float posterBlend = saturate(styleCurve * 4.2 * COMICS_STYLE_BOOST + giMask * 0.35);

        float skyReduce = lerp(1.0, 0.28, skySunMask);
        float sunReduce = lerp(1.0, 0.12, sunCoreMask);
        posterBlend *= skyReduce * sunReduce;

        styled = lerp(styled, posterized, posterBlend * (1.15 * COMICS_STYLE_BOOST) * (1.0 - pureSkyMask));
    }
#endif

#if COMICS_ENABLE_HATCHING
    if ((PURE_SKY_DISABLE_STYLING == 0) || pureSkyMask < 0.999)
    {
        float shade = Luma(styled);
        float hatch = HatchFactor(IN.uv, shade);
        float hatchSuppression = lerp(1.0, 0.04, skySunMask);
        hatchSuppression = lerp(hatchSuppression, 0.0, sunCoreMask);
        hatchSuppression *= (1.0 - pureSkyMask);
        float giHatchBoost = giMask * 0.40 * hatchSuppression * COMICS_STYLE_BOOST;
        styled = lerp(styled, styled * lerp(1.0, hatch, hatchSuppression),
                      saturate((STYLE_BLEND * 1.25 + giHatchBoost) * COMICS_STYLE_BOOST));
    }
#endif

    float edge = 0.0;
#if COMICS_ENABLE_OUTLINES
    if ((PURE_SKY_EDGE_SUPPRESS == 0) || pureSkyMask < 0.999)
    {
        edge = EdgeFactor(IN.uv);
#if OUTLINE_DILATE_NEIGHBORS
        if (pureSkyMask < 0.999)
        {
            uint w, h;
            SceneColor.GetDimensions(w, h);
            if (w == 0 || h == 0)
            {
                w = (uint) ((SCREEN_SIZE.x > 0) ? SCREEN_SIZE.x : 1920);
                h = (uint) ((SCREEN_SIZE.y > 0) ? SCREEN_SIZE.y : 1080);
            }
            float2 texel = 1.0 / float2(w, h);
            float eL = EdgeFactor(IN.uv + float2(-texel.x * OUTLINE_WIDTH_PX, 0));
            float eR = EdgeFactor(IN.uv + float2(texel.x * OUTLINE_WIDTH_PX, 0));
            float eT = EdgeFactor(IN.uv + float2(0, texel.y * OUTLINE_WIDTH_PX));
            float eB = EdgeFactor(IN.uv + float2(0, -texel.y * OUTLINE_WIDTH_PX));
            edge = max(edge, max(max(eL, eR), max(eT, eB)));
        }
#endif
        edge = pow(saturate(edge), EDGE_POWER);
    }
#endif

    float outlineAlpha = edge * OUTLINE_STRENGTH * 3.6 * COMICS_STYLE_BOOST;
    outlineAlpha *= lerp(1.0, 0.22, skySunMask * (1.0 - pureSkyMask));
    outlineAlpha *= (1.0 - sunCoreMask);
    outlineAlpha = max(outlineAlpha, edge * OUTLINE_MIN_VISIBILITY);

    float3 inkColor = float3(0.04, 0.04, 0.045);
    float3 baseCol = styled * (1.0 - giMask * 0.07 * COMICS_STYLE_BOOST);

    float3 finalCol = baseCol * (1.0 - outlineAlpha * 0.88) +
                      inkColor * (outlineAlpha * 0.88);

    finalCol = lerp(finalCol, original, pureSkyMask);
    finalCol = lerp(finalCol, original, sunCoreMask * 0.80);

    return float4(saturate(finalCol), 1);
#endif
}