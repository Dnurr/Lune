#version 440

layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec2 v_uv;      // texture coordinates, (0, 0) = top-left of the image
layout(location = 3) in vec4 v_tangent; // world space, along +u; w: bitangent = cross(v_normal, v_tangent.xyz) * v_tangent.w
layout(location = 0) out vec4 fragColor;

#define MAX_MATERIAL_PARAMS 32 // MaterialCompiler::MaxParams
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 color;
    vec4 outline;
    vec4 viewport;
    vec4 cameraPos;
    vec4 material;  // x: roughness, y: metallic (0..1), z: alpha cutoff (Clip materials; 0 otherwise),
                    // w: alpha mode of the node material (0 opaque, 1 clip, 2 blend; see Surface.alpha)
    vec4 materialParams[MAX_MATERIAL_PARAMS]; // node material: values typed into its nodes (see materialSurface())
};

// Scene lights, filled by the Renderer once per frame (binding 1, shared by every mesh).
// A scene without enabled lights gets one default sun, so lightCount.x is never 0.
#define MAX_LIGHTS 8 // Renderer::MaxLights
struct Light
{
    vec4 position;  // xyz: world position (point lights), w: type (0 directional, 1 point)
    vec4 direction; // xyz: normalized direction the light travels (directional lights)
    vec4 color;     // rgb: linear color, w: intensity -> radiance = color.rgb * color.w
};
layout(std140, binding = 1) uniform lightsBuf {
    ivec4 lightCount; // x: number of lights in use
    Light lights[MAX_LIGHTS];
};

// Image-based lighting (IblMaps), shared by every mesh. Without an Environment entity
// the maps hold a sky/ground gradient, so they're always valid.
layout(std140, binding = 2) uniform environmentBuf {
    mat4 envRotation; // world direction -> environment direction: mat3(envRotation) * dir
    vec4 envParams;   // x: intensity, y: prefiltered max mip (levels - 1), z: background blur, w: background shown
};
// Diffuse irradiance per normal direction (already divided by PI): kD * albedo * irradiance.
layout(binding = 3) uniform samplerCube irradianceMap;
// Specular reflection, blurred per roughness: mip = roughness * envParams.y (textureLod).
layout(binding = 4) uniform samplerCube prefilteredMap;
// Split-sum BRDF terms: texture(brdfLut, vec2(NdotV, roughness)).rg = (scale, bias) for F0.
layout(binding = 5) uniform sampler2D brdfLut;

// Shadows (ShadowMaps), shared by every mesh. Every light with `shadows` on casts them, up
// to MAX_DIR_SHADOWS directional and MAX_POINT_SHADOWS point lights (in scene order); the
// others light without shadows. Each shadowed light has a slot: a tile in its type's atlas.
//
// Directional light, slot s:  cascaded. Pick the cascade from the distance along the camera's view:
//                               depth = dot(v_world - cameraPos.xyz, cameraForward.xyz)
//                               cascade = how many of cascadeSplits.xyz are < depth (0..3);
//                               past cascadeSplits.w there are no meshes, treat as lit.
//                             t = s * SHADOW_CASCADES + cascade;
//                             p = (dirShadowMatrix[t] * vec4(v_world, 1.0)).xyz  (orthographic: w = 1)
//                             and dirShadowTile[t], dirShadowRange[t] for that cascade.
// Point light, slot s:        face = major axis of (v_world - light position), in the order
//                             +X -X +Y -Y +Z -Z;  c = pointShadowMatrix[s * 6 + face] * vec4(v_world, 1.0),
//                             p = c.xyz / c.w  (perspective: divide by w)
// p.xy is the uv in the atlas, p.z the depth to compare. Each tile's uv rectangle is in
// *ShadowTile (xy min, zw max): keep filter samples inside it (they'd read the neighbor tile),
// and treat points outside a directional tile as lit. Point faces have PointFaceBorder (24)
// extra texels around the 90° view, so samples up to 24 texels away stay valid near a face's
// edge; keep point light search and filter radii within that.
#define MAX_DIR_SHADOWS 4
#define SHADOW_CASCADES 4
#define MAX_POINT_SHADOWS 4
layout(std140, binding = 6) uniform shadowBuf {
    vec4 lightShadow[MAX_LIGHTS];                     // per lights[i]: x slot (-1 = no shadows), y size
                                                      // (point: radius; directional: angular diameter, degrees),
                                                      // z softness (filter radius in texels), w unused
    mat4 dirShadowMatrix[MAX_DIR_SHADOWS * SHADOW_CASCADES]; // world -> (atlas uv, depth), per cascade
    vec4 dirShadowTile[MAX_DIR_SHADOWS * SHADOW_CASCADES];   // uv rectangle of each cascade's tile
    vec4 dirShadowRange[MAX_DIR_SHADOWS * SHADOW_CASCADES];  // x: world distance covered by depth 0..1 (linear),
                                                             // y: world size of one texel (differs per cascade)
    mat4 pointShadowMatrix[MAX_POINT_SHADOWS * 6];    // world -> (atlas uv, depth), per face
    vec4 pointShadowTile[MAX_POINT_SHADOWS * 6];      // uv rectangle of each face's tile
    vec4 pointShadowRange[MAX_POINT_SHADOWS];         // x: near, y: far. Depth is perspective; distance along
                                                      // the face's axis = near * far / (far - depth * (far - near)).
                                                      // z: tan of half the face's field of view (a bit over 1),
                                                      // w: face size in texels. World size of one texel at
                                                      // distance d along the axis = 2 * d * z / w
    vec4 shadowAtlas;                                 // xy: texel size (uv) of the directional atlas, zw: point atlas
    vec4 cascadeSplits;                               // far distance of cascades 0..3 along the camera's view
    vec4 cameraForward;                               // xyz: the camera's view direction (unit)
    vec4 translucencyDir;                             // directional atlas uv -> shadowTranslucency uv: uv * xy + zw
    vec4 translucencyPoint;                           // point atlas uv -> shadowTranslucency uv: uv * xy + zw
};
// Each atlas twice. The comparison sampler gives the filtered shadow test:
//     texture(dirShadowMap, vec3(uv, depth))  ->  1 = lit (depth <= the stored one), 0 = in shadow,
// already blended across the 4 nearest texels. The raw one gives the stored depth itself
// (texture(dirShadowDepth, uv).r), e.g. to search for blockers (PCSS). The shadow passes
// already apply a slope-scaled depth bias.
layout(binding = 7) uniform sampler2DShadow dirShadowMap;
layout(binding = 8) uniform sampler2D dirShadowDepth;
layout(binding = 9) uniform sampler2DShadow pointShadowMap;
layout(binding = 10) uniform sampler2D pointShadowDepth;

// Translucent shadows. Blend materials (glass, smoke) don't write the depth atlases: each
// shadowed light's view of them goes into shadowTranslucency instead, at half resolution,
// with the same tiles as the atlases (one region for each atlas: map the atlas uv p.xy
// with translucencyDir / translucencyPoint above). Per texel:
//   rgb  the fraction of the light that gets through them, multiplied over every layer
//        (1 = all of it, i.e. no translucent caster; an alpha 0.5 surface: 0.5)
//   a    linear depth of the nearest translucent caster, 0..1 (1 = none)
//          directional: the same depth as the atlas, compare it with p.z
//          point:       distance along the face's axis / far, i.e. a * far is comparable
//                       with the receiver's distance along the axis (dReceiver)
// Only a receiver farther from the light than that depth is behind them: its light is
// multiplied by rgb (the translucent surface itself, or a vase standing on a glass table,
// must not be). Linear filtering, like the comparison samplers: average a few samples
// for soft edges. Opaque shadows still come from the depth atlases.
layout(binding = 19) uniform sampler2D shadowTranslucency;

// Direction in the world -> direction to sample the environment maps with.
vec3 toEnvironment(vec3 dir)
{
    return mat3(envRotation) * dir;
}

const float PI = 3.14159265359;

// ---------------------------------------------------------------------------
// Material: the surface at this pixel (Claude: declarations and the default).
//
// Without a node material, materialSurface() returns the entity's own values (below).
// With one, the editor replaces everything between the two marker lines with the node
// functions of material_nodes.glsl and a materialSurface() generated from the material's
// graph, and compiles this shader again at runtime. Values typed into the nodes arrive in
// materialParams[]. Don't remove the markers.
//
// main() takes the surface from here:  Surface surf = materialSurface();
//
// Textures of the material: its image texture inputs, in order (sRGB images are already
// linear when sampled; data images as they are). Unused slots hold a 1x1 texture.
// ---------------------------------------------------------------------------
layout(binding = 11) uniform sampler2D materialTexture0; // MaterialCompiler::FirstTextureBinding
layout(binding = 12) uniform sampler2D materialTexture1;
layout(binding = 13) uniform sampler2D materialTexture2;
layout(binding = 14) uniform sampler2D materialTexture3;
layout(binding = 15) uniform sampler2D materialTexture4;
layout(binding = 16) uniform sampler2D materialTexture5;
layout(binding = 17) uniform sampler2D materialTexture6;
layout(binding = 18) uniform sampler2D materialTexture7; // MaterialCompiler::MaxTextures = 8

struct Surface
{
    vec3 baseColor;  // linear
    float roughness; // 0..1
    float metallic;  // 0..1
    vec3 normal;     // world space, normalized (normal maps bend it); default: normalize(v_normal)
    float occlusion; // ambient occlusion, 0..1 (1 = none): meant for the ambient (environment) light
    vec3 emission;   // linear, added to the outgoing light
    float alpha;     // 0..1, used by the material's alpha mode (material.w):
                     //   opaque (0): ignore it, write alpha 1
                     //   clip (1):   discard the pixel where alpha < material.z, the rest is opaque
                     //   blend (2):  see-through: the pipeline blends PREMULTIPLIED color,
                     //               result = fragColor.rgb + (1 - fragColor.a) * what's behind,
                     //               so write vec4(color * alpha, alpha) (light that a clear
                     //               surface still reflects, e.g. specular, may stay unscaled)
                     // Discarding where alpha < material.z works for every mode (z is 0 unless clip).
    float absorption; // 0..1, blend materials: the share of light a fully opaque (alpha 1) surface
                      // absorbs instead of letting through; used by its translucent shadow
                      // (shadow_material.glsl). Default 0.
};
//@material-begin
Surface materialSurface()
{
    Surface s;
    s.baseColor = color.rgb;
    s.roughness = material.x;
    s.metallic = material.y;
    s.normal = normalize(v_normal);
    s.occlusion = 1.0;
    s.emission = vec3(0.0);
    s.alpha = 1.0;
    s.absorption = 0.0;
    return s;
}
//@material-end

float DistGGX(vec3 N, vec3 H, float a)
{
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return a2 / denom;
}

float GeomShlickGGX(float NdotV, float k)
{
    float denom = NdotV * (1.0 - k) + k;
    
    return NdotV / denom;
}

float GeomSmith(vec3 N, vec3 V, vec3 L, float k)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeomShlickGGX(NdotV, k);
    float ggx2 = GeomShlickGGX(NdotL, k);

    return ggx1 * ggx2;
}

vec3 Fresnel(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

vec3 FresnelRoughness(float NdotV, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NdotV, 5.0);
}

const vec2 poisson[16] = vec2[](
    vec2(-0.94201624, -0.39906216),
    vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870),
    vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432),
    vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845),
    vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554),
    vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023),
    vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507),
    vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367),
    vec2( 0.14383161, -0.14100790)
);

vec3 Translucency(int type, vec3 p, vec4 tile, vec2 texel, float radius, mat2 rot, float dReceiver, float depthScale, float bias)
{
    vec4 map = type == 0 ? translucencyDir : translucencyPoint;
    vec3 sum = vec3(0.0);

    for (int k = 0; k < 16; k++)
    {
        vec2 uv = clamp(p.xy + rot * poisson[k] * radius * texel, tile.xy, tile.zw);
        vec4 t = textureLod(shadowTranslucency, uv * map.xy + map.zw, 0.0);

        bool behind = t.a * depthScale < dReceiver - bias;

        sum += behind ? t.rgb : vec3(1.0);
    }

    return sum / 16.0;
}

vec3 Shadow(int i, int type, vec3 N)
{
    vec3 lit = vec3(0.0);

    int slot = int(lightShadow[i].x);
    int t = 0;

    float size = lightShadow[i].y;
    float softness = lightShadow[i].z;

    float dReceiver;
    float texelWorld;
    vec3 pos;
    vec3 p;
    vec4 tile;
    vec2 texel;
    float spread = 2.0 * tan(radians(size) / 2.0);

    float angle = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));

    float sum = 0.0;
    int count = 0;
    float near = pointShadowRange[slot].x, far = pointShadowRange[slot].y;
    
    if (type == 0)
    {
        float camDepth = dot(v_world - cameraPos.xyz, cameraForward.xyz);
        if (camDepth > cascadeSplits.w) return vec3(1.0); // too far = no shadows

        int cascade = int(camDepth > cascadeSplits.x) + int(camDepth > cascadeSplits.y)
                    + int(camDepth > cascadeSplits.z);

        float start = cascade == 0 ? 0.0 : cascadeSplits[cascade - 1];
        float end = cascadeSplits[cascade];
        float fraction = (camDepth - start) / (end - start);

        float blend = smoothstep(0.85, 1.0, fraction);
        float noise = angle / 6.2831853;
        if (cascade < SHADOW_CASCADES - 1 && blend > noise) cascade++;

        t = slot * SHADOW_CASCADES + cascade;
        texelWorld = dirShadowRange[t].y;
        pos = v_world + N * texelWorld * 2.0; 

        p = (dirShadowMatrix[t] * vec4(pos, 1.0)).xyz;
        tile = dirShadowTile[t];
     
        if (any(lessThan(p.xy, tile.xy)) || any(greaterThan(p.xy, tile.zw))) return vec3(1.0);
     
        texel = shadowAtlas.xy;
        
        dReceiver = p.z * dirShadowRange[t].x;
    }
    else
    {
        vec3 d = v_world - lights[i].position.xyz;
        vec3 a = abs(d);
        int face;
        if (a.x >= a.y && a.x >= a.z) face = d.x > 0.0 ? 0 : 1;
        else if (a.y >= a.z)          face = d.y > 0.0 ? 2 : 3;
        else                          face = d.z > 0.0 ? 4 : 5;
        
        t = slot * 6 + face;

        float tanHalf = pointShadowRange[slot].z;
        float axisDist = max(a.x, max(a.y, a.z));
        texelWorld = 2.0 * axisDist * tanHalf / pointShadowRange[slot].w;
        pos = v_world + N * texelWorld * 2.0;

        vec4 c = pointShadowMatrix[t] * vec4(pos, 1.0);
        p = c.xyz / c.w;
        tile = pointShadowTile[t];
        texel = shadowAtlas.zw;
        dReceiver = near * far / (far - p.z * (far - near));
    }

    float searchTexels = type == 0 ? min(dReceiver * spread / texelWorld, 16.0) : 16.0;
    searchTexels = max(searchTexels, softness);
    vec2  searchUV = searchTexels * texel;

    for (int k = 0; k < 16; k++)
    {
        vec2 uv = clamp(p.xy + rot * poisson[k] * searchUV, tile.xy, tile.zw);
        float z = type == 0 ? textureLod(dirShadowDepth, uv, 0.0).r : textureLod(pointShadowDepth, uv, 0.0).r;
        float dist = type == 0 ? z * dirShadowRange[t].x
                               : near * far / (far - z * (far - near));
        if (z < p.z) { sum += dist; count++; }
    }

    float depthScale = type == 0 ? dirShadowRange[t].x : far;
    float bias = 2.0 * 2.0 * texelWorld;
    if (count == 0) return Translucency(type, p, tile, texel, searchTexels, rot, dReceiver, depthScale, bias);

    float dBlocker = sum / float(count);
    float penumbraWorld = type == 0 ? (dReceiver - dBlocker) * spread : 2.0 * size * (dReceiver - dBlocker) / dBlocker;
    float penumbraTexels = penumbraWorld / texelWorld;
    float radius = max(min(penumbraTexels, searchTexels), softness);

    for (int k = 0; k < 16; k++)
    {
        vec2 uv = clamp(p.xy + rot * poisson[k] * radius * texel, tile.xy, tile.zw);
        lit += type == 0 ? textureLod(dirShadowMap, vec3(uv, p.z), 0.0) : textureLod(pointShadowMap, vec3(uv, p.z), 0.0);
    }

    return lit / 16.0 * Translucency(type, p, tile, texel, radius, rot, dReceiver, depthScale, bias);
}

// Cook-Torrance PBR Model
void main()
{
    Surface surf = materialSurface();
    if (surf.alpha < material.z) discard;
    
    vec3 Ng = normalize(v_normal);
    vec3 N = surf.normal;

    vec3 V = normalize(cameraPos.xyz - v_world);
    
    if (dot(Ng, V) < 0.0) { Ng = -Ng; N = -N; }

    vec3 albedo = surf.baseColor;
    float roughness = max(surf.roughness, 0.045);
    float metallic = surf.metallic;
    float alpha = material.w > 1.5 ? surf.alpha : 1.0;

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    float NdotV = max(dot(N, V), 0.0);
    
    vec3 kS = FresnelRoughness(NdotV, F0, roughness);

    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(prefilteredMap, toEnvironment(R), roughness * envParams.y).rgb;
    vec3 lut = texture(brdfLut, vec2(NdotV, roughness)).rgb;
    vec3 ambientSpec = prefiltered * (kS * lut.r + lut.g) * envParams.x * surf.occlusion;

    vec3 final = vec3(0.0);

    for (int i = 0; i < lightCount.x; i++)
    {
        vec3 radiance = lights[i].color.rgb * lights[i].color.w;
        vec3 L;
        float dist2;
        if (lights[i].position.w > 0.5) // point
        {
            vec3 toLight = lights[i].position.xyz - v_world;
            dist2 = max(dot(toLight, toLight), 1e-4);
            L = toLight * inversesqrt(dist2);
            radiance /= dist2;
        }
        else L = -lights[i].direction.xyz;
        
        vec3 H = normalize(V + L);

        float D = DistGGX(N, H, roughness * roughness);
        float G = GeomSmith(N, V, L, (roughness + 1.0) * (roughness + 1.0) / 8.0);
        vec3 F = Fresnel(max(dot(H, V), 0.0), F0);

        float NdotL = max(dot(N, L), 0.0);
        
        vec3 spec = (D * G * F) / ((4.0 * NdotV * NdotL) + 0.001);
        
        vec3 kD = (1.0 - F) * (1.0 - metallic);

        vec3 shadow = vec3(1.0);
        int slot = int(lightShadow[i].x);
        if (slot >= 0)
            shadow = Shadow(i, int(lights[i].position.w), Ng);

        final += (kD * albedo / PI + spec) * radiance * NdotL * shadow;
    }

    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 irradiance = texture(irradianceMap, toEnvironment(N)).rgb;
    vec3 ambient = kD * albedo * irradiance * envParams.x * surf.occlusion;
    
    final *= alpha;
    final += ambient + ambientSpec + surf.emission;

    fragColor = vec4(final, surf.alpha);
}