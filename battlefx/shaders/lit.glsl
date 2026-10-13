// shared lighting for the hand-written arena / mesh shaders: three.js r149 legacy lights (physicallyCorrectLights off):
// hemisphere + one directional sun + up to 6 point lights (distance cutoff, decay 2), Lambert diffuse, exp2 fog.
uniform vec3 uHemiSky; uniform vec3 uHemiGround; uniform float uHemiI;
uniform vec3 uSunDir; uniform vec3 uSunCol; uniform float uSunI;
uniform vec4 uLightPos[6];   // xyz, distance
uniform vec4 uLightCol[6];   // rgb * intensity
uniform int uLightCount;
uniform vec3 uFogCol; uniform float uFogDensity;
vec3 lightAt(vec3 p, vec3 n) {
  vec3 irr = mix(uHemiGround, uHemiSky, 0.5 * n.y + 0.5) * uHemiI;
  irr += uSunCol * uSunI * max(dot(n, normalize(uSunDir)), 0.0);
  for (int i = 0; i < 6; i++) {
    if (i >= uLightCount) break;
    vec3 d = uLightPos[i].xyz - p; float dist = length(d); float cut = uLightPos[i].w;
    float fall = cut > 0.0 ? pow(clamp(1.0 - dist / cut, 0.0, 1.0), 2.0) : 1.0;
    irr += uLightCol[i].rgb * fall * max(dot(n, d / max(dist, 1e-4)), 0.0);
  }
  return irr;
}
vec3 fogged(vec3 c, float depth) { float f = 1.0 - exp(-uFogDensity * uFogDensity * depth * depth); return mix(c, uFogCol, clamp(f, 0.0, 1.0)); }
