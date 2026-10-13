#version 330 core
// hand-written: how a game applies packet.battlers[b] to ITS OWN Pokemon sprite (a full-colour RGBA texture):
// camera-facing quad at the battler's world position, transformed by the battler's 2x2 matrix (GBA pixels)
uniform mat4 projectionMatrix; uniform mat4 viewMatrix;
uniform vec3 uCenter; uniform vec3 uRight; uniform vec3 uUp; uniform float uK; uniform vec2 uSize; uniform vec4 uMatrix; uniform vec2 uFlip; uniform float uScale;
in vec2 corner;
out vec2 vUv;
void main() {
  vec2 t = corner * uSize * uScale;                                   // texture-space offset (GBA px, y down)
  vec2 s = vec2(uMatrix.x * t.x + uMatrix.y * t.y, uMatrix.z * t.x + uMatrix.w * t.y);   // -> screen offset
  vec3 p = uCenter + uRight * (s.x * uK) - uUp * (s.y * uK);
  vUv = corner + 0.5; if (uFlip.x > 0.5) vUv.x = 1.0 - vUv.x; if (uFlip.y > 0.5) vUv.y = 1.0 - vUv.y;
  gl_Position = projectionMatrix * viewMatrix * vec4(p, 1.0);
}
