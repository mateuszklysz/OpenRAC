shader_type spatial;
// A moby class's glow faces, as the game's glow pass draws them: texture times the class's glow
// colour, unlit, blended over the solid faces at the texture's alpha. The next pass of
// lit_moby.gdshader's material (rc1/import_lit.gd). Display only.
render_mode cull_disabled, unshaded, blend_mix, depth_draw_never;

#include "res://rc1/lit_moby.gdshaderinc"

void fragment() {
	if (glowing < 0.5) {
		discard;
	}
	vec4 texel = clamped ? texture(albedo_clamp, UV) : texture(albedo_repeat, UV);
	ALBEDO = texel.rgb * light;
	ALPHA = clamp(texel.a, 0.0, 1.0);
}
