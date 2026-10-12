#version 330 core
// hand-written: flat-shaded lit effect meshes (normals from screen derivatives, as three.js flatShading)
//#include lit.glsl
uniform vec3 uBaseColor; uniform vec3 uEmissive; uniform vec3 uRim; uniform float uRoughness; uniform float uMetalness; uniform vec3 uCamPos;
in vec3 vWorld; in vec3 vColor; in float vDepth;
out vec4 fragColor;
void main() {
  vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
  vec3 albedo = uBaseColor * vColor * (1.0 - uMetalness * 0.5);
  vec3 c = albedo * lightAt(vWorld, n) + uEmissive;
  // a small specular lobe for the glossy kinds (crystal, gem, ice): GGX-like highlight from the sun
  vec3 v = normalize(uCamPos - vWorld); vec3 h = normalize(normalize(uSunDir) + v);
  float sp = pow(max(dot(n, h), 0.0), mix(200.0, 4.0, uRoughness)) * (1.0 - uRoughness) * uSunI;
  c += uSunCol * sp * 0.6;
  // v3: rim light (view.js dataMesh: totalEmissiveRadiance += uRim * pow(1 - |n.v|, 2)), lifts the silhouette of cut stone / ice
  c += uRim * pow(1.0 - clamp(abs(dot(n, v)), 0.0, 1.0), 2.0);
  fragColor = vec4(fogged(c, vDepth), 1.0);
}
