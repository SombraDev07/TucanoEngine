// Cry-parity rain: view-space streaks (camera-stable) + island puddles + lens drops.

cbuffer RainCB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4x4 viewMtx;      // world → view
  float4 cameraPos;      // xyz, time
  float4 screenSize;     // w, h, 1/w, 1/h
  float4 rainAmount;     // amount, dropsAmount, dropsSpeed, dropsLighting
  float4 rainWet;        // diffuseDarkening, puddlesAmount, glossBoost, splashesAmount
  float4 rainAnim;       // wind.x, wind.z, rippleFrame, mistAmount
  float4 rainColor;      // rgb, layers
  float4 rainVolume;     // center.xyz, radius
  float4 sceneRain0;     // streakSpeed, _, _, _
  float4 sceneRain1;     // dropsLighting, streakIntensity, 0, maxViewDist
};

Texture2D depthTex : register(t0);
Texture2D src0Tex : register(t1);
Texture2D src1Tex : register(t2);
Texture2D src2Tex : register(t3);
Texture2D puddleMaskTex : register(t4);
Texture2D rainSpatterTex : register(t5);
Texture2D surfaceFlowTex : register(t6);
Texture2DArray rainRippleTex : register(t7);
Texture2D rainOccTex : register(t8);
Texture2D bloomTex : register(t9);

SamplerState linearSamp : register(s0);
SamplerState wrapSamp : register(s1);
SamplerState pointSamp : register(s2);

struct VSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
};

struct GBufferOut {
  float4 albedo : SV_Target0;
  float4 normal : SV_Target1;
  float4 orm : SV_Target2;
};

VSOut VSMain(uint id : SV_VertexID) {
  VSOut o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
  return o;
}

float3 reconstructWorld(float2 uv, float depth) {
  float4 clip = float4(uv * 2 - 1, depth, 1);
  clip.y = -clip.y;
  float4 w = mul(invViewProj, clip);
  return w.xyz / w.w;
}

float2 clampWind() {
  return clamp(float2(rainAnim.x, rainAnim.y), -1.0, 1.0);
}

float hash21(float2 p) {
  float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return frac((p3.x + p3.y) * p3.z);
}

float2 hash22(float2 p) {
  float n = hash21(p);
  return float2(n, hash21(p + n + 19.19));
}

float2 unpackXYNormal(float4 n) {
  return n.xy * 2.0 - 1.0;
}

float volumeAttenuation(float3 world) {
  float3 d = world - rainVolume.xyz;
  float r = max(rainVolume.w, 1.0);
  return saturate(1.0 - dot(d, d) / (r * r));
}

// Shared puddle island mask (localized, not full-floor wash)
float puddleIsland(float2 xz, float puddleAmt) {
  float a = puddleMaskTex.Sample(wrapSamp, xz * 0.045).r;
  float b = puddleMaskTex.Sample(wrapSamp, xz * 0.012 + 9.1).r;
  float c = rainSpatterTex.Sample(wrapSamp, xz * 0.09).r;
  float mask = a * 0.5 + b * 0.35 + (1.0 - c) * 0.15;
  // Threshold → clear islands; puddleAmt widens them
  float lo = lerp(0.55, 0.28, saturate(puddleAmt * 0.5));
  float hi = lerp(0.78, 0.5, saturate(puddleAmt * 0.5));
  return smoothstep(lo, hi, mask);
}

float4 PSCopy(VSOut input) : SV_Target {
  return src0Tex.SampleLevel(pointSamp, input.uv, 0);
}

// Very soft occlusion — avoid stair-step popping when camera moves
float4 PSOcclusion(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  if (depth >= 0.9995) {
    return 1.0;
  }
  float3 world = reconstructWorld(input.uv, depth);
  float2 wind = clampWind();
  float3 rainDir = normalize(float3(-wind.x * 0.25, 1.0, -wind.y * 0.25));
  float blocked = 0.0;
  float stepLen = 0.55;
  [unroll] for (int i = 1; i <= 6; ++i) {
    float3 p = world + rainDir * (stepLen * (float)i);
    float4 clip = mul(viewProj, float4(p, 1));
    if (clip.w <= 0.0)
      break;
    float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
    if (any(uv < 0.02) || any(uv > 0.98))
      break;
    float dScene = depthTex.SampleLevel(pointSamp, uv, 0).r;
    float dRay = clip.z / clip.w;
    if (dScene < dRay - 0.002) {
      blocked += 0.2;
    }
  }
  return saturate(1.0 - blocked);
}

// ---------------------------------------------------------------------------
// DeferredRainGBuffer — clear puddle islands + strong gloss/ripples
// ---------------------------------------------------------------------------
GBufferOut PSDeferredGBuffer(VSOut input) {
  GBufferOut o;
  float4 albedo = src0Tex.SampleLevel(pointSamp, input.uv, 0);
  float4 nEnc = src1Tex.SampleLevel(pointSamp, input.uv, 0);
  float4 orm = src2Tex.SampleLevel(pointSamp, input.uv, 0);
  o.albedo = albedo;
  o.normal = nEnc;
  o.orm = orm;

  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  float amt = saturate(rainAmount.x);
  if (depth >= 0.9995 || amt < 0.001) {
    return o;
  }

  float3 world = reconstructWorld(input.uv, depth);
  float atten = volumeAttenuation(world) * amt;
  float occ = rainOccTex.SampleLevel(linearSamp, input.uv, 0).r;
  atten *= lerp(0.55, 1.0, occ);
  if (atten < 0.001) {
    return o;
  }

  float3 n = normalize(nEnc.xyz * 2.0 - 1.0);
  float2 xz = world.xz;
  float2 wind = clampWind();

  float isUp = saturate(n.y * 5.0 - 3.6);
  float puddleAmt = saturate(rainWet.y);
  float island = puddleIsland(xz, puddleAmt);
  float puddle = saturate(isUp * island * puddleAmt * atten);

  // Thin wet film everywhere on up-facing (subtle), puddles on top
  float film = saturate(isUp * amt * atten * 0.25);
  float wet = max(puddle, film * 0.35);

  // Ripples — strong only inside islands
  float rippleFrame = floor(rainAnim.z);
  float2 rippleUV = xz * 0.18 + wind * cameraPos.w * -0.08;
  float2 rippleN = unpackXYNormal(rainRippleTex.SampleLevel(wrapSamp, float3(rippleUV, rippleFrame), 0));
  float2 rippleUV2 = xz * 0.31 - wind * cameraPos.w * 0.05;
  float2 rippleN2 = unpackXYNormal(
      rainRippleTex.SampleLevel(wrapSamp, float3(rippleUV2, (rippleFrame + 8) % 24), 0));
  float2 rip = normalize(rippleN + rippleN2 * 0.65 + 1e-5);
  n.xy = lerp(n.xy, rip * 0.9, puddle * 0.75 * rainWet.z);

  float2 flowN = unpackXYNormal(surfaceFlowTex.Sample(wrapSamp, xz * 0.4 + wind * 0.02));
  n.xy += flowN * puddle * 0.18;
  n = normalize(n);

  float roughness = orm.g;
  float porosity = saturate(((1.0 - roughness) - 0.45) / 0.4);

  // Darker puddle cores, milder film
  float darkPuddle = lerp(1.0, lerp(0.55, 0.18, porosity * rainWet.x), puddle);
  float darkFilm = lerp(1.0, 0.88, film * rainWet.x * 0.5);
  albedo.rgb *= darkPuddle * darkFilm;

  // Mirror puddles (very low roughness in cores)
  float core = saturate((island - 0.45) * 2.5) * puddle;
  float smoothPuddle = lerp(0.72, 0.96, core) * saturate(rainWet.z);
  float smoothFilm = lerp(1.0 - roughness, 0.55, film * 0.5);
  float newSmooth = max(smoothFilm, lerp(1.0 - roughness, smoothPuddle, puddle));
  orm.g = 1.0 - saturate(newSmooth);

  // Bump F0 slightly in puddles via orm.a if used as dielectric F0
  orm.a = saturate(orm.a + puddle * 0.15);

  // Soft stable splash in puddles only
  float2 splashCell = floor(xz * 5.0);
  float splashRnd = hash21(splashCell);
  float splashLife = frac(cameraPos.w * 0.4 + splashRnd * 5.3);
  float splashPulse = smoothstep(0.0, 0.1, splashLife) * smoothstep(0.4, 0.15, splashLife);
  float splash = splashPulse * step(0.7, splashRnd) * puddle * rainWet.w * 0.4;
  albedo.rgb = lerp(albedo.rgb, albedo.rgb * 1.08 + 0.02, splash);
  n.xy += flowN * splash * 0.3;
  n = normalize(n);

  // Walls: damp only — no stretched flow/streak look
  float wall = saturate(1.0 - abs(n.y)) * atten * amt * 0.12;
  albedo.rgb *= lerp(1.0, 0.92, wall * rainWet.x);
  orm.g = saturate(orm.g - wall * 0.08);

  o.albedo = float4(saturate(albedo.rgb), albedo.a);
  o.normal = float4(saturate(n * 0.5 + 0.5), nEnc.a);
  o.orm = orm;
  return o;
}

// Virtual rain plane in front of camera (NOT surface UV — avoids painting walls)
float3 rainPlaneView(float2 uv, float planeZ, float aspect) {
  // Approximate view-space XY on a plane at view-Z = planeZ (LH, Z forward)
  float2 ndc = uv * 2.0 - 1.0;
  ndc.y = -ndc.y;
  float fovTan = 0.7; // ~approx 60° half-ish scale; good enough for streaks
  return float3(ndc.x * aspect * fovTan * planeZ, ndc.y * fovTan * planeZ, planeZ);
}

float texturedRainLayerVS(float3 viewP, float t, float layer, float2 wind) {
  float scale = lerp(0.45, 0.95, layer * 0.4);
  float speed = (1.15 + layer * 0.5) * max(sceneRain0.x, 0.15);
  float2 tc;
  tc.x = viewP.x * scale + layer * 7.3 + wind.x * t * 0.25;
  tc.y = -viewP.y * scale * 1.2 - t * speed;
  tc.x += (-viewP.y) * wind.x * 0.08; // mild slant, not wall stretch

  float rain = src1Tex.Sample(wrapSamp, tc * 0.28).r;
  float rain2 = src1Tex.Sample(wrapSamp, tc * 0.48 + 0.27).r;
  float a = saturate(rain * 0.65 + rain2 * 0.4);
  float2 nxy = unpackXYNormal(src2Tex.Sample(wrapSamp, tc * 0.28));
  a *= lerp(0.45, 1.2, saturate(length(nxy)));
  a *= lerp(1.0, 0.4, layer / 3.0);
  return a;
}

float rainStreakLayerVS(float3 viewP, float t, float layer, float2 wind) {
  float density = 2.2 + layer * 1.3;
  float lengthScale = 1.6 + layer * 0.55;
  float speed = (1.5 + layer * 0.55) * max(sceneRain0.x, 0.2);

  float2 slant = normalize(float2(wind.x * 0.35, 1.0));
  float2 p;
  p.x = viewP.x * density + layer * 5.1;
  p.y = (-viewP.y) * lengthScale - t * speed;
  p.x += p.y * slant.x * 0.2 + wind.x * t * 0.1;

  float2 cell = floor(p);
  float2 f = frac(p);
  float rnd = hash21(cell + layer * 19.0);
  if (rnd < 0.55)
    return 0.0;

  float cx = rnd * 0.6 + 0.2;
  float halfW = lerp(0.012, 0.032, hash21(cell.yx));
  float streak = exp(-pow((f.x - cx) / max(halfW, 1e-4), 2.0));
  float head = smoothstep(0.0, 0.2, f.y) * smoothstep(1.0, 0.5, f.y);
  streak *= head * lerp(0.5, 1.3, pow(smoothstep(0.15, 0.5, f.y), 2.0));
  return streak * rnd;
}

// Linearize depth roughly for soft intersection (works with 0-near 1-far Z)
float approxViewZ(float depth) {
  // Perspective reverse-ish: map nonlinear Z to meters (tuned for 0.1–300)
  float z = saturate(depth);
  return lerp(0.1, 300.0, z * z);
}

float4 PSStreaks(VSOut input) : SV_Target {
  float3 hdr = src0Tex.Sample(linearSamp, input.uv).rgb;
  float amt = saturate(rainAmount.x);
  float inten = sceneRain1.y * amt;
  if (inten < 0.001) {
    return float4(hdr, 1);
  }

  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  float occ = rainOccTex.SampleLevel(linearSamp, input.uv, 0).r;
  float t = cameraPos.w;
  float2 wind = clampWind();
  int layers = (int)clamp(rainColor.w, 1.0, 3.0);
  float aspect = screenSize.x * screenSize.w;
  float sceneZ = approxViewZ(depth);

  float streaks = 0.0;
  float texRain = 0.0;
  [unroll] for (int i = 0; i < 3; ++i) {
    if (i >= layers)
      break;
    float w = (i == 0) ? 1.0 : (i == 1 ? 0.62 : 0.38);
    // Planes at ~4m / 12m / 28m in front of camera
    float planeZ = lerp(4.0, 28.0, (float)i / 2.0);
    float3 viewP = rainPlaneView(input.uv, planeZ, aspect);

    // Soft clip against scene: rain only in empty air (not painted on walls)
    float soft = 1.0;
    if (depth < 0.9995) {
      soft = saturate((sceneZ - planeZ) * 0.35);
      soft = smoothstep(0.0, 1.0, soft);
    }

    streaks += rainStreakLayerVS(viewP, t, (float)i, wind) * w * soft;
    texRain += texturedRainLayerVS(viewP, t, (float)i, wind) * w * soft;
  }

  float skyBoost = depth > 0.998 ? 1.25 : 1.0;
  float vis = skyBoost * lerp(0.75, 1.0, occ);

  streaks = saturate(streaks * inten * 1.05 * vis);
  texRain = saturate(texRain * inten * 1.1 * vis);

  float3 col = rainColor.rgb * float3(0.82, 0.9, 1.05);
  float3 bloom = bloomTex.SampleLevel(linearSamp, input.uv, 0).rgb;
  hdr += col * texRain * 0.45;
  hdr += col * streaks * 0.85;
  hdr += col * streaks * streaks * 0.3;
  hdr += bloom * (texRain * 0.18 + streaks * 0.1) * sceneRain1.x;
  return float4(max(hdr, 0.0), 1);
}

// Extra puddle specular sheen after lighting (reads normal in src1)
float4 PSPuddleSpec(VSOut input) : SV_Target {
  float3 hdr = src0Tex.Sample(linearSamp, input.uv).rgb;
  float amt = saturate(rainAmount.x) * saturate(rainWet.y);
  if (amt < 0.001) {
    return float4(hdr, 1);
  }
  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  if (depth >= 0.9995) {
    return float4(hdr, 1);
  }

  float3 world = reconstructWorld(input.uv, depth);
  float3 n = normalize(src1Tex.SampleLevel(pointSamp, input.uv, 0).xyz * 2.0 - 1.0);
  float isUp = saturate(n.y * 5.0 - 3.6);
  float island = puddleIsland(world.xz, saturate(rainWet.y));
  float puddle = saturate(isUp * island * amt);
  if (puddle < 0.01) {
    return float4(hdr, 1);
  }

  float3 V = normalize(cameraPos.xyz - world);
  float ndotv = saturate(dot(n, V));
  float fres = pow(1.0 - ndotv, 4.0);

  // Fake env: blur HDR neighborhood + sky tint
  float2 texel = screenSize.zw;
  float3 env = 0;
  env += src0Tex.Sample(linearSamp, input.uv + float2(3, 0) * texel).rgb;
  env += src0Tex.Sample(linearSamp, input.uv + float2(-3, 0) * texel).rgb;
  env += src0Tex.Sample(linearSamp, input.uv + float2(0, 3) * texel).rgb;
  env += src0Tex.Sample(linearSamp, input.uv + float2(0, -3) * texel).rgb;
  env *= 0.25;
  env = lerp(env, rainColor.rgb * 0.35, 0.25);

  float gloss = saturate(rainWet.z);
  hdr += env * fres * puddle * gloss * 0.85;
  hdr += fres * puddle * 0.08; // hot highlight
  return float4(max(hdr, 0.0), 1);
}

float4 PSMist(VSOut input) : SV_Target {
  float3 hdr = src0Tex.Sample(linearSamp, input.uv).rgb;
  float mistAmt = rainAnim.w * saturate(rainAmount.x);
  if (mistAmt < 0.001) {
    return float4(hdr, 1);
  }
  float depth = depthTex.Sample(linearSamp, input.uv).r;
  float fog = mistAmt * 0.1;
  if (depth < 0.9995) {
    float3 world = reconstructWorld(input.uv, depth);
    float dist = length(cameraPos.xyz - world);
    fog = saturate(dist / max(sceneRain1.w, 1.0)) * mistAmt * 0.22;
    fog *= lerp(0.7, 1.0, rainOccTex.SampleLevel(linearSamp, input.uv, 0).r);
  }
  float3 tint = rainColor.rgb * float3(0.85, 0.92, 1.05) * 0.16;
  hdr = lerp(hdr, tint, fog);
  return float4(hdr, 1);
}

float4 PSDrops(VSOut input) : SV_Target {
  float3 hdr = src0Tex.Sample(linearSamp, input.uv).rgb;
  float amount = rainAmount.y * saturate(rainAmount.x);
  if (amount < 0.001) {
    return float4(hdr, 1);
  }

  float t = cameraPos.w * rainAmount.z;
  float2 uv = input.uv;
  float aspect = screenSize.x * screenSize.w;
  float drops = 0.0;
  float2 refrOffset = 0.0;
  float2 wind = clampWind();

  [unroll] for (int i = 0; i < 20; ++i) {
    float fi = (float)i;
    float2 rnd = hash22(float2(fi * 0.37 + 1.1, fi * 1.9 + 3.3));
    float life = frac(t * (0.07 + rnd.y * 0.1) + rnd.x);
    float2 center;
    center.x = rnd.x + sin(t * 0.5 + fi) * 0.01 * wind.x;
    center.y = lerp(-0.05, 1.08, life);
    float size = 0.008 + rnd.y * 0.014;
    float2 d = (uv - center) * float2(aspect, 1.0);
    float dist = length(d);
    float blob = smoothstep(size, size * 0.2, dist);
    float trailMask = smoothstep(size * 1.1, 0.0, abs(d.x)) *
                      smoothstep(0.0, 0.1, -d.y) * smoothstep(0.2, 0.0, -d.y);
    float drop = max(blob, trailMask * 0.35) * (1.0 - life * 0.8);
    drops += drop;
    refrOffset += normalize(d + 1e-5) * blob * size * 2.0;
  }

  drops = saturate(drops * amount * 0.9);
  float3 refr = src0Tex.Sample(linearSamp, uv + refrOffset * amount * 0.012).rgb;
  float3 highlight = rainColor.rgb * 0.06 * rainAmount.w + 0.04;
  hdr = lerp(hdr, refr * 1.03 + highlight, drops);
  return float4(hdr, 1);
}

struct SceneVSIn {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};
struct SceneVSOut {
  float4 pos : SV_Position;
  float4 tcProj : TEXCOORD0;
  float4 baseTC : TEXCOORD1;
  float4 baseTC2 : TEXCOORD2;
  float4 blendWeights : TEXCOORD3;
};
SceneVSOut VSSceneRain(SceneVSIn IN) {
  SceneVSOut o = (SceneVSOut)0;
  o.pos = float4(0, 0, 0, 1);
  return o;
}
float4 PSSceneRain(SceneVSOut IN) : SV_Target { return 0; }
