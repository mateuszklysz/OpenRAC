shader_type spatial;
// A tie or shrub lit as the game lights it at level load: the texture times its instance's colour
// for the vertex's light slot (UV2.x). The colours are a row per instance of <family>s_light.png
// (0x80 = 1.0); the row is the instance's light_row (rc1/import_lit.gd, godot.py). Display only.
// Unshaded: the colours are all the light it gets, as in the game.
render_mode cull_disabled, unshaded;

uniform sampler2D albedo_repeat : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D albedo_clamp : source_color, filter_linear_mipmap, repeat_disable;
uniform bool clamped = false;
uniform bool cutout = false;
uniform sampler2D light_colours : filter_nearest, repeat_disable;
instance uniform int light_row = -1;

varying vec3 light;

void vertex() {
	light = vec3(1.0);
	// An instance without a row (one added in the editor) shows the texture as it is.
	if (light_row >= 0) {
		vec3 c = texelFetch(light_colours, ivec2(int(UV2.x + 0.5), light_row), 0).rgb * (255.0 / 128.0);
		// The game multiplies in its own (gamma) space; Godot in linear light, as the terrain's.
		light = pow(c, vec3(2.2));
	}
}

void fragment() {
	vec4 texel = clamped ? texture(albedo_clamp, UV) : texture(albedo_repeat, UV);
	ALBEDO = texel.rgb * light;
	if (cutout) {
		ALPHA = texel.a;
		ALPHA_SCISSOR_THRESHOLD = 0.5;
	}
}
