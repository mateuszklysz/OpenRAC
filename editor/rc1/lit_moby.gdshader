shader_type spatial;
// A moby lit as the game lights it (MobyProc and its vertex lighting; port/assets/geometry/
// moby_lighting.cpp): the ambient plus two directional lights of the level's light sets, each
// max(d, d * -|K|) with d the normal's dot with the direction to the light, times the vertex's
// colour multiplier (COLOR), then times the texture. Display only, in floats: close to the game's
// bytes, not bit for bit. Unshaded: these are all the light it gets.
// A class with a glow colour draws its glow packets' faces in lit_moby_glow.gdshader, its next pass.
render_mode cull_disabled, unshaded;

#include "res://rc1/lit_moby.gdshaderinc"


void fragment() {
	// The glow faces are the next pass's.
	if (glowing > 0.5) {
		discard;
	}
	vec4 texel = clamped ? texture(albedo_clamp, UV) : texture(albedo_repeat, UV);
	ALBEDO = texel.rgb * light;
	if (cutout) {
		ALPHA = texel.a;
		ALPHA_SCISSOR_THRESHOLD = 0.5;
	}
}
