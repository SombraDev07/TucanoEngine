// Cry-parity rain: view-space streaks (camera-stable) + island puddles + lens drops.

cbuffer RainCB : register(b1) {
  float4x4 invViewProj;
  float4x4 viewProj;
  float4x4 viewMtx;      // world → view
  float4x4 rainOccVP;    // world → rain-space clip (Cry occluder)
  float4 cameraPos;      // xyz, time
  float4 screenSize;     // w, h, 1/w, 1/h
  float4 rainAmount;     // amount, dropsAmount, dropsSpeed, dropsLighting
  float4 rainWet;        // diffuseDarkening, puddlesAmount, glossBoost, splashesAmount
  float4 rainAnim;       // wind.x, wind.z, rippleFrame, mistAmount
  float4 rainColor;      // rgb, layers
  float4 rainVolume;     // center.xyz, radius
  float4 sceneRain0;     // streakSpeed, puddlesSSR, sceneRainIntensity, splashAmt
  float4 sceneRain1;     // dropsLighting, streakIntensity, sceneLayer, maxViewDist
  float4 rainLight0;     // sunToScene.xyz, tanHalfFovY
  float4 rainLight1;     // sun radiance rgb (premultiplied), weatherMapValid
  float4x4 invRainOccVP; // rain-space clip → world (particle ground collision)
  float4 rainMisc;       // dt, rainingAmount (0 while drying), particleRadius, wetnessExtent
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
Texture2D ssrTex : register(t10);
Texture2D gbufNormalTex : register(t11); // scene normals (composite pass)
Texture2D weatherTex : register(t12);    // cloud weather map (R = coverage), camera-centered ±4 km
Texture2D wetnessTex : register(t13);    // accumulated wetness, world-tiled (extent = rainMisc.w)

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

// Accumulated wetness at a world position (world-tiled map, wrap-sampled).
float wetnessAt(float2 worldXZ) {
  float ext = max(rainMisc.w, 1.0);
  return wetnessTex.SampleLevel(wrapSamp, worldXZ / ext, 0).r;
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

float puddleIslandLevel(float2 xz, float puddleAmt) {
  float a = puddleMaskTex.SampleLevel(wrapSamp, xz * 0.045, 0).r;
  float b = puddleMaskTex.SampleLevel(wrapSamp, xz * 0.012 + 9.1, 0).r;
  float c = rainSpatterTex.SampleLevel(wrapSamp, xz * 0.09, 0).r;
  float mask = a * 0.5 + b * 0.35 + (1.0 - c) * 0.15;
  float lo = lerp(0.55, 0.28, saturate(puddleAmt * 0.5));
  float hi = lerp(0.78, 0.5, saturate(puddleAmt * 0.5));
  return smoothstep(lo, hi, mask);
}

float4 PSCopy(VSOut input) : SV_Target {
  return src0Tex.SampleLevel(pointSamp, input.uv, 0);
}

// Cry rain-space occluder: compare against depth map rendered along rain direction
float4 PSOcclusion(VSOut input) : SV_Target {
  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  if (depth <= 1e-4) { // sky (DepthColor clears to 0)
    return 1.0;
  }
  float3 world = reconstructWorld(input.uv, depth);
  float4 clip = mul(rainOccVP, float4(world, 1.0));
  if (clip.w <= 1e-4) {
    return 1.0;
  }
  float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
  if (any(uv < 0.001) || any(uv > 0.999)) {
    return 1.0;
  }
  float mapD = src0Tex.SampleLevel(pointSamp, uv, 0).r;
  float pointZ = saturate(clip.z / clip.w);
  // Geometry closer to sky than this surface blocks rain
  float blocked = (mapD + 0.002 < pointZ) ? 1.0 : 0.0;
  // Soft edge
  float soft = saturate((pointZ - mapD) * 40.0);
  blocked = max(blocked, soft * 0.35);
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
  if (depth <= 1e-4) {
    return o;
  }

  float3 world = reconstructWorld(input.uv, depth);
  // Temporal wetness: soaks while it rains, keeps the ground wet (and drying) afterwards.
  float wetAccum = wetnessAt(world.xz);
  float amt = max(wetAccum, saturate(rainMisc.y) * 0.15);
  if (amt < 0.005) {
    return o;
  }
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
  // Ripples only while drops are actually falling; still puddles stay mirror-like after rain.
  n.xy = lerp(n.xy, rip * 0.9, puddle * 0.75 * rainWet.z * saturate(0.15 + rainMisc.y));

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
  // View-space XY on a plane at view-Z = planeZ (LH, Z forward), using the camera's real FOV.
  float2 ndc = uv * 2.0 - 1.0;
  ndc.y = -ndc.y;
  float fovTan = rainLight0.w > 1e-3 ? rainLight0.w : 0.7;
  return float3(ndc.x * aspect * fovTan * planeZ, ndc.y * fovTan * planeZ, planeZ);
}

// Cloud weather-map coverage → local rain factor. Falls back to 1 when no weather map is bound.
// rainLight1.w: 0 = no map; else 0.5 + 0.5 * global coverage (heavy overcast rains everywhere).
float rainWeatherAt(float2 worldXZ) {
  if (rainLight1.w < 0.25) {
    return 1.0;
  }
  float globalCov = saturate(rainLight1.w * 2.0 - 1.0);
  float floorRain = 0.35 * smoothstep(0.25, 0.75, globalCov);
  float2 uv = (worldXZ - cameraPos.xz) / 8000.0 + 0.5;
  if (any(uv < 0.0) || any(uv > 1.0)) {
    return max(floorRain, 0.5);
  }
  float cov = weatherTex.SampleLevel(linearSamp, uv, 0).r;
  // Clear sky → no rain; dense deck → full rain.
  return max(smoothstep(0.12, 0.5, cov), floorRain);
}

// Backscatter tint for falling rain: bright against the sun, faint front-lit (HG phase).
float3 rainStreakLight(float3 V) {
  float3 sunL = normalize(-rainLight0.xyz);
  float cosTh = dot(V, sunL);
  float g = 0.65;
  float g2 = g * g;
  float ph = (1.0 - g2) / (12.566 * pow(abs(1.0 + g2 - 2.0 * g * cosTh), 1.5));
  return rainColor.rgb * float3(0.82, 0.9, 1.05) * 0.3 + rainLight1.rgb * ph;
}

// Highest surface under the rain at `probe` (from the Cry rain-space occluder depth).
// Returns world position of that surface; y = -1e5 when outside the occluder window.
float3 rainSpaceSurface(float3 probe) {
  float4 clip = mul(rainOccVP, float4(probe, 1.0));
  float2 uv = clip.xy / max(clip.w, 1e-4) * float2(0.5, -0.5) + 0.5;
  if (any(uv < 0.001) || any(uv > 0.999)) {
    return float3(probe.x, -1e5, probe.z);
  }
  float mapD = src1Tex.SampleLevel(pointSamp, uv, 0).r;
  float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
  float4 w = mul(invRainOccVP, float4(ndc, mapD, 1.0));
  return w.xyz / max(w.w, 1e-4);
}

// Wetness accumulation / drying (512^2 R16F ping-pong, world-tiled).
// t1 = previous wetness, t2 = rain-space depth (unused here), t12 = weather map.
float4 PSWetnessUpdate(VSOut input) : SV_Target {
  float ext = max(rainMisc.w, 1.0);
  float prev = src0Tex.SampleLevel(pointSamp, input.uv, 0).r;
  // Texel world position: pick the tile instance nearest the camera (for the weather lookup).
  float2 cand = input.uv * ext;
  cand -= ext * round((cand - cameraPos.xz) / ext);
  float wcov = rainWeatherAt(cand);
  float dt = rainMisc.x;
  float raining = rainMisc.y;
  float wet = prev + raining * wcov * dt * 0.22; // ~6 s of steady rain to fully soak
  wet -= dt / 75.0;                              // ~75 s to fully dry
  return float4(saturate(wet), 0, 0, 1);
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

float3 applyStreaks(float3 hdr, float2 uv, float depth, float3 V, float wcov) {
  float2 input_uv = uv;
  float amt = saturate(rainAmount.x);
  float inten = sceneRain1.y * amt * wcov;
  if (inten < 0.001) {
    return hdr;
  }

  float occ = rainOccTex.SampleLevel(linearSamp, input_uv, 0).r;
  float t = cameraPos.w;
  float2 wind = clampWind();
  int layers = (int)clamp(rainColor.w, 1.0, 3.0);
  float aspect = screenSize.x * screenSize.w;
  // Sky (DepthColor = 0) → rain falls freely through the whole air column.
  float sceneDist = 1e6;
  if (depth > 1e-4) {
    sceneDist = length(reconstructWorld(input_uv, depth) - cameraPos.xyz);
  }

  float streaks = 0.0;
  float texRain = 0.0;
  [unroll] for (int i = 0; i < 3; ++i) {
    if (i >= layers)
      break;
    float w = (i == 0) ? 1.0 : (i == 1 ? 0.62 : 0.38);
    // Planes at ~4m / 12m / 28m in front of camera
    float planeZ = lerp(4.0, 28.0, (float)i / 2.0);
    float3 viewP = rainPlaneView(input_uv, planeZ, aspect);

    // Soft clip against scene: rain only in empty air (not painted on walls)
    float soft = smoothstep(0.0, 1.0, saturate((sceneDist - planeZ) * 0.35));

    streaks += rainStreakLayerVS(viewP, t, (float)i, wind) * w * soft;
    texRain += texturedRainLayerVS(viewP, t, (float)i, wind) * w * soft;
  }

  float skyBoost = depth <= 1e-4 ? 1.25 : 1.0;
  float vis = skyBoost * lerp(0.75, 1.0, occ);

  streaks = saturate(streaks * inten * 1.05 * vis);
  texRain = saturate(texRain * inten * 1.1 * vis);

  // Backlit rain: bright toward the sun (HG backscatter), faint front-lit.
  float3 col = rainStreakLight(V);
  float3 bloom = bloomTex.SampleLevel(linearSamp, input_uv, 0).rgb;
  hdr += col * texRain * 0.45;
  hdr += col * streaks * 0.85;
  hdr += col * streaks * streaks * 0.3;
  hdr += bloom * (texRain * 0.18 + streaks * 0.1) * sceneRain1.x;
  return max(hdr, 0.0);
}

// Puddle mirror: SSR buffer + short local march + Fresnel water F0
float3 applyPuddleSpec(float3 hdr, float2 input_uv, float depth) {
  if (depth <= 1e-4) { // sky
    return hdr;
  }

  float3 world = reconstructWorld(input_uv, depth);
  // Wetness-driven: reflections persist (and dry out) after the rain stops.
  float amt = max(wetnessAt(world.xz), saturate(rainMisc.y) * 0.2) * saturate(rainWet.y);
  if (amt < 0.001) {
    return hdr;
  }
  float3 n = normalize(gbufNormalTex.SampleLevel(pointSamp, input_uv, 0).xyz * 2.0 - 1.0);
  float isUp = saturate(n.y * 5.0 - 3.6);
  float island = puddleIsland(world.xz, saturate(rainWet.y));
  float puddle = saturate(isUp * island * amt);
  if (puddle < 0.01) {
    return hdr;
  }

  float3 V = normalize(cameraPos.xyz - world);
  float ndotv = saturate(dot(n, V));
  float F0 = 0.02;
  float fres = F0 + (1.0 - F0) * pow(1.0 - ndotv, 5.0);
  fres = lerp(fres, 1.0, pow(1.0 - ndotv, 3.0) * 0.35);

  float3 R = reflect(-V, n);
  float gloss = saturate(rainWet.z);

  float4 ssr = ssrTex.SampleLevel(linearSamp, input_uv, 0);
  float3 refl = ssr.rgb;
  float conf = saturate(ssr.a * 1.35);

  if (conf < 0.55) {
    float2 dirSS = float2(R.x, -R.y);
    float dirLen = length(dirSS);
    float3 marchCol = 0;
    float marchHit = 0;
    if (dirLen > 1e-4) {
      dirSS /= dirLen;
      float2 texelM = screenSize.zw;
      float2 uv = input_uv;
      float prevD = depth;
      const int steps = 24;
      float stride = 3.0;
      [loop] for (int i = 1; i <= steps; ++i) {
        uv += dirSS * texelM * stride;
        if ((i & 3) == 0) {
          stride *= 1.25;
        }
        if (any(uv < 0.01) || any(uv > 0.99)) {
          break;
        }
        float d = depthTex.SampleLevel(pointSamp, uv, 0).r;
        if (d > 1e-4 && d < depth - 0.002 && d < prevD - 0.001) {
          marchCol = min(src0Tex.SampleLevel(linearSamp, uv, 0).rgb, 6.0);
          float edge = saturate(1.0 - max(abs(uv.x - 0.5), abs(uv.y - 0.5)) * 2.0);
          marchHit = edge * saturate(1.0 - float(i) / float(steps));
          break;
        }
        prevD = d;
      }
    }
    float3 sky = rainColor.rgb * float3(0.55, 0.65, 0.85) * 0.45;
    float3 fallback = lerp(sky, marchCol, marchHit);
    refl = lerp(fallback, refl, conf);
    conf = max(conf, marchHit * 0.85);
  }

  float2 texel = screenSize.zw;
  float3 soft = 0;
  soft += src0Tex.SampleLevel(linearSamp, input_uv + float2(4, 0) * texel, 0).rgb;
  soft += src0Tex.SampleLevel(linearSamp, input_uv + float2(-4, 0) * texel, 0).rgb;
  soft += src0Tex.SampleLevel(linearSamp, input_uv + float2(0, 4) * texel, 0).rgb;
  soft += src0Tex.SampleLevel(linearSamp, input_uv + float2(0, -4) * texel, 0).rgb;
  soft *= 0.25;
  refl = lerp(soft * 0.65 + rainColor.rgb * 0.08, refl, saturate(conf + 0.2));

  float strength = puddle * gloss * (0.75 + conf * 0.55) * saturate(sceneRain0.y);
  hdr += refl * fres * strength;
  hdr += fres * puddle * 0.04 * saturate(sceneRain0.y);
  return max(hdr, 0.0);
}

float3 applyMist(float3 hdr, float2 input_uv, float depth, float3 V, float wcov) {
  float mistAmt = rainAnim.w * saturate(rainAmount.x) * lerp(0.25, 1.0, wcov);
  if (mistAmt < 0.001) {
    return hdr;
  }
  float fog = mistAmt * 0.1;
  if (depth > 1e-4) { // geometry: distance-based mist
    float3 world = reconstructWorld(input_uv, depth);
    float dist = length(cameraPos.xyz - world);
    fog = saturate(dist / max(sceneRain1.w, 1.0)) * mistAmt * 0.22;
    fog *= lerp(0.7, 1.0, rainOccTex.SampleLevel(linearSamp, input_uv, 0).r);
  }
  // Mist picks up sun backscatter like the streaks do (rain veil glows toward the sun).
  float3 tint = rainColor.rgb * float3(0.85, 0.92, 1.05) * 0.14 + rainStreakLight(V) * 0.12;
  return lerp(hdr, tint, fog);
}

// Merged rain post: puddle reflections + mist + lit streaks in a single full-res pass.
// No early-out on rain amount: puddle reflections must persist while the ground dries.
float4 PSRainComposite(VSOut input) : SV_Target {
  float3 hdr = src0Tex.Sample(linearSamp, input.uv).rgb;
  float depth = depthTex.SampleLevel(pointSamp, input.uv, 0).r;
  float3 V = normalize(reconstructWorld(input.uv, 1.0) - cameraPos.xyz);
  float3 world = cameraPos.xyz + V * 1500.0; // sky: probe the weather map along the ray
  if (depth > 1e-4) {
    world = reconstructWorld(input.uv, depth);
  }
  float wcov = rainWeatherAt(world.xz);

  hdr = applyPuddleSpec(hdr, input.uv, depth);
  hdr = applyMist(hdr, input.uv, depth, V, wcov);
  hdr = applyStreaks(hdr, input.uv, depth, V, wcov);
  return float4(max(hdr, 0.0), 1);
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
  float2 uv : TEXCOORD0;
  float4 clip : TEXCOORD1;
  float3 world : TEXCOORD2;
  float layer : TEXCOORD3;
};

// Root consts: viewProj + world (same RootXform as GBuffer/Shadow)
cbuffer SceneRoot : register(b0) {
  float4x4 rootViewProj;
  float4x4 rootWorld;
};

SceneVSOut VSSceneRain(SceneVSIn IN) {
  SceneVSOut o;
  // Mesh: (x,y) radial, z = height axis (Cry). Map to Y-up.
  float3 lp = float3(IN.position.x, IN.position.z, IN.position.y);
  float4 wp = mul(rootWorld, float4(lp, 1.0));
  o.world = wp.xyz;
  o.pos = mul(rootViewProj, wp);
  o.clip = o.pos;
  o.layer = sceneRain1.z;
  float2 wind = clampWind();
  float t = cameraPos.w;
  float speed = max(sceneRain0.x, 0.2);
  o.uv = float2(IN.position.x * 0.5 + 0.5, -IN.position.z * 0.35 - t * speed * 0.15) + wind * 0.05;
  return o;
}

float4 PSSceneRain(SceneVSOut IN) : SV_Target {
  float inten = saturate(rainAmount.x) * saturate(sceneRain0.z);
  if (inten < 0.001) {
    discard;
  }
  float2 ndc = IN.clip.xy / max(IN.clip.w, 1e-4);
  float2 uv = ndc * float2(0.5, -0.5) + 0.5;
  if (any(uv < 0.0) || any(uv > 1.0)) {
    discard;
  }

  float sceneD = depthTex.SampleLevel(pointSamp, uv, 0).r;
  float rainD = saturate(IN.clip.z / max(IN.clip.w, 1e-4));
  // Soft clip: keep rain in front of opaque surfaces (sky = 0 → no clip)
  float soft = 1.0;
  if (sceneD > 1e-4) {
    soft = saturate((sceneD - rainD) * 80.0 + 0.5);
    soft = smoothstep(0.0, 1.0, soft);
  }
  float occ = rainOccTex.SampleLevel(linearSamp, uv, 0).r;

  float rain = src1Tex.Sample(wrapSamp, IN.uv * float2(2.5, 4.0)).r;
  float rain2 = src1Tex.Sample(wrapSamp, IN.uv * float2(3.8, 6.0) + 0.31).r;
  float a = saturate(rain * 0.55 + rain2 * 0.45);
  float2 nxy = unpackXYNormal(src2Tex.Sample(wrapSamp, IN.uv * 2.5));
  a *= lerp(0.5, 1.15, saturate(length(nxy)));
  a *= soft * lerp(0.7, 1.0, occ) * inten;

  // Radial fade of cone shell
  float radial = length(float2(IN.world.x - cameraPos.x, IN.world.z - cameraPos.z));
  a *= saturate(1.0 - radial / max(sceneRain1.w * 0.6, 5.0));
  a *= 0.22; // volume density

  if (a < 0.002) {
    discard;
  }
  float3 col = rainColor.rgb * float3(0.85, 0.92, 1.05);
  return float4(col * a, a);
}

// World splash billboards — SV_VertexID expands 384 particles × 6 verts
struct SplashVSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float life : TEXCOORD1;
  float2 centerUV : TEXCOORD2;
};

// ---------------------------------------------------------------------------
// Stateless GPU rain drops (RDR2-style): each particle's position is a pure
// function of (id, time); ground collision via the rain-space occluder depth.
// The splash pass reuses the same state, so every splash is the landing of a
// real drop — same spot, same instant.
// ---------------------------------------------------------------------------

struct DropState {
  float3 pos;      // current world position
  float3 vel;      // fall velocity (m/s)
  float3 ground;   // impact surface position (y = -1e5 outside occluder window)
  float phase;     // 0..1 within fall cycle
  float phaseHit;  // phase at which the drop hits the surface
  float alive;     // density gate (rain amount × local cloud coverage)
  float rnd;       // per-drop random
};

DropState rainDropState(uint pid) {
  DropState s;
  float t = cameraPos.w;
  float radius = max(rainMisc.z, 4.0);
  float span = radius * 2.0;
  float2 rnd = hash22(float2(float(pid) * 0.611 + 0.37, float(pid) * 1.377 + 9.13));
  float2 rnd2 = hash22(rnd * 41.7 + float(pid) * 0.0137);
  s.rnd = rnd2.y;

  // World-anchored tile that always surrounds the camera (correct parallax when moving).
  float2 anchor = rnd * span;
  float2 rel = frac((anchor - cameraPos.xz) / span + 0.5) - 0.5;
  float2 xz = cameraPos.xz + rel * span;

  float speed = lerp(8.5, 13.5, rnd2.x);
  const float colH = 24.0;
  float topY = cameraPos.y + colH * 0.6;
  s.phase = frac(t * speed / colH + rnd2.y * 7.31);
  float2 wind = clampWind();
  s.vel = float3(wind.x * 1.8, -speed, wind.y * 1.8);
  float fallTime = s.phase * colH / speed;
  float3 p = float3(xz.x, topY - s.phase * colH, xz.y);
  p.xz += wind * 1.8 * fallTime;
  s.pos = p;
  s.ground = rainSpaceSurface(p);
  s.phaseHit = saturate((topY - s.ground.y) / colH);

  float density = saturate(rainMisc.y) * rainWeatherAt(xz);
  s.alive = frac(rnd2.y * 39.77) < density ? 1.0 : 0.0;
  return s;
}

struct DropVSOut {
  float4 pos : SV_Position;
  float2 uv : TEXCOORD0;
  float fade : TEXCOORD1;
  float4 clip : TEXCOORD2;
  float3 tint : TEXCOORD3;
};

static const float2 kQuadCorners[6] = {float2(-1, -1), float2(1, -1), float2(-1, 1),
                                       float2(-1, 1),  float2(1, -1), float2(1, 1)};

DropVSOut VSRainDrops(uint vid : SV_VertexID) {
  DropVSOut o;
  uint pid = vid / 6;
  float2 qc = kQuadCorners[vid % 6];
  o.pos = float4(2, 2, 0, 1); // offscreen default
  o.uv = qc * 0.5 + 0.5;
  o.fade = 0;
  o.clip = o.pos;
  o.tint = 0;

  DropState s = rainDropState(pid);
  if (s.alive < 0.5 || s.phase >= s.phaseHit) {
    return o;
  }
  float3 toCam = cameraPos.xyz - s.pos;
  float dist = length(toCam);
  float radius = max(rainMisc.z, 4.0);
  if (dist < 0.35 || dist > radius) {
    return o;
  }
  toCam /= dist;

  // Quad stretched along the fall velocity (motion-blurred drop).
  float3 velDir = normalize(s.vel);
  float3 side = normalize(cross(velDir, toCam) + 1e-5);
  float len = clamp(length(s.vel) * 0.035, 0.22, 0.55);
  float width = 0.010 + dist * 0.0016;
  float3 p = s.pos + side * (qc.x * width) + velDir * (qc.y * len * 0.5);
  o.pos = mul(viewProj, float4(p, 1));
  o.clip = o.pos;
  o.fade = saturate(1.0 - dist / radius) * lerp(0.6, 1.0, s.rnd);
  o.tint = rainStreakLight(-toCam);
  return o;
}

float4 PSRainDrops(DropVSOut IN) : SV_Target {
  if (IN.fade < 0.005) {
    discard;
  }
  // Soft clip against opaque scene (same convention as SceneRain cones).
  float2 ndc = IN.clip.xy / max(IN.clip.w, 1e-4);
  float2 suv = ndc * float2(0.5, -0.5) + 0.5;
  float sceneD = depthTex.SampleLevel(pointSamp, saturate(suv), 0).r;
  float rainD = saturate(IN.clip.z / max(IN.clip.w, 1e-4));
  float soft = 1.0;
  if (sceneD > 1e-4) {
    soft = smoothstep(0.0, 1.0, saturate((sceneD - rainD) * 120.0 + 0.5));
  }
  float shape = saturate(1.0 - abs(IN.uv.x * 2.0 - 1.0));
  shape *= smoothstep(0.0, 0.15, IN.uv.y) * smoothstep(1.0, 0.75, IN.uv.y);
  float a = shape * shape * IN.fade * soft * 0.5;
  if (a < 0.004) {
    discard;
  }
  return float4(IN.tint * a * 2.2, a);
}

SplashVSOut VSSplash(uint vid : SV_VertexID) {
  SplashVSOut o;
  uint pid = vid / 6;
  float2 qc = kQuadCorners[vid % 6];
  o.pos = float4(2, 2, 0, 1);
  o.uv = qc * 0.5 + 0.5;
  o.life = 0;
  o.centerUV = 0;

  DropState s = rainDropState(pid);
  const float splashLen = 0.1; // fraction of the fall cycle (~0.2 s)
  float lifeP = (s.phase - s.phaseHit) / splashLen;
  if (s.alive < 0.5 || lifeP < 0.0 || lifeP >= 1.0 || s.ground.y < -1e4 ||
      saturate(sceneRain0.w) < 0.01) {
    return o;
  }
  float dist = distance(cameraPos.xyz, s.ground);
  float radius = max(rainMisc.z, 4.0);
  if (dist > radius) {
    return o;
  }

  float pulse = smoothstep(0.0, 0.25, lifeP) * smoothstep(1.0, 0.45, lifeP);
  float size = lerp(0.04, 0.11, s.rnd) * lerp(0.5, 1.0, lifeP) * saturate(sceneRain0.w);

  float3 toCam = normalize(cameraPos.xyz - s.ground);
  float3 up = float3(0, 1, 0);
  float3 right = normalize(cross(up, toCam) + 1e-5);
  up = normalize(cross(toCam, right));
  float3 pos = s.ground + float3(0, 0.02, 0) + right * (qc.x * size) + up * (qc.y * size);
  o.pos = mul(viewProj, float4(pos, 1));
  o.life = pulse * saturate(1.0 - dist / radius);
  o.centerUV = s.ground.xz * 0.13;
  return o;
}

float4 PSSplash(SplashVSOut IN) : SV_Target {
  if (IN.life < 0.01) {
    discard;
  }
  float2 d = IN.uv * 2.0 - 1.0;
  float r = length(d);
  float ring = smoothstep(0.95, 0.55, r) * smoothstep(0.15, 0.45, r);
  float fill = exp(-r * r * 4.0) * 0.35;
  float a = (ring + fill) * IN.life * saturate(rainWet.w) * 0.6;
  float spat = rainSpatterTex.Sample(wrapSamp, IN.uv * 0.5 + IN.centerUV).r;
  a *= lerp(0.6, 1.2, spat);
  if (a < 0.01) {
    discard;
  }
  float3 col = rainColor.rgb * 1.1 + 0.05;
  return float4(col * a, a);
}
