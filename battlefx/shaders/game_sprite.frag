#version 330 core
// hand-written: the battler state on a full-colour sprite. tint (linear: gray, mul, add), HD flash, light, GBA alpha,
// fade; glow pass (uGlowPass = 1): additive silhouette in the glow colour
uniform sampler2D uTex; uniform vec3 uTintMul; uniform vec3 uTintAdd; uniform float uGray; uniform vec4 uFlash; uniform vec3 uLight; uniform vec3 uLightAdd;
uniform vec2 uBlend; uniform float uFade; uniform vec4 uGlow; uniform float uGlowPass;
in vec2 vUv;
out vec4 fragColor;
void main() {
  vec4 t = texture(uTex, vec2(vUv.x, 1.0 - vUv.y));   // image row 0 = top
  float cov = t.a * uFade; if (cov < 0.01) discard;
  if (uGlowPass > 0.5) { fragColor = vec4(uGlow.rgb * uGlow.a * cov, 0.0); return; }
  vec3 c = pow(t.rgb, vec3(2.2));
  c = mix(c, vec3(dot(c, vec3(0.299, 0.587, 0.114))), clamp(uGray, 0.0, 1.0));
  c = clamp(c * uTintMul + uTintAdd, 0.0, 1.0);
  c = mix(c, uFlash.rgb, uFlash.a);
  c = min(c * uLight + uLightAdd, vec3(1.0));
  fragColor = vec4(c * uBlend.x * cov, 1.0 - cov + cov * uBlend.y);   // blended like the GBA sprites: ONE, SRC_ALPHA
}
