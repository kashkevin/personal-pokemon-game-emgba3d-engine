#version 330 core
// hand-written (not generated): instanced effect meshes (rocks, crystals, cut ice...), three.js MeshStandardMaterial look
uniform mat4 projectionMatrix; uniform mat4 viewMatrix;
in vec3 position; in vec3 normal; in vec3 vtxColor;                   // vtxColor: per-face shade (v3 cut meshes; 1 for the others)
in vec4 iM0; in vec4 iM1; in vec4 iM2; in vec4 iM3; in vec3 iColor;   // per instance: column-major model matrix + colour
out vec3 vWorld; out vec3 vColor; out float vDepth;
void main() {
  mat4 M = mat4(iM0, iM1, iM2, iM3); vec4 w = M * vec4(position, 1.0);
  vWorld = w.xyz; vColor = iColor * vtxColor; vec4 v = viewMatrix * w; vDepth = -v.z;   // three.js: material colour x vertex colour x instance colour
  gl_Position = projectionMatrix * v;
}
