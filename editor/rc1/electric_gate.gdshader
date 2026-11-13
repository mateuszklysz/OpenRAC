// An electric gate's beams as draw_electric_gate_beams draws them (effects.py, rc1/electric_gate.gd):
// added quads in effect texture 19, each beam's colour pulsing between two reds over 64 frames,
// a fifth of the way on from the beam below. UV2: the beam (0 to 4) and whether the quad is the
// inner one. Colours are the GS's, 0x80 = 1.0; Godot's blend_add adds ALBEDO * ALPHA.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, blend_add;

uniform sampler2D picture : source_color, filter_linear, repeat_enable;
uniform vec4 outer_from;
uniform vec4 outer_to;
uniform vec4 inner_from;
uniform vec4 inner_to;
// The time in seconds: the cutscene preview's, else the editor's own (rc1/electric_gate.gd).
uniform float seconds = 0.0;

varying vec4 colour;

void vertex() {
	// (frame & 0x3F) / 63 of a turn, and 2 pi / 5 more for each beam up.
	float angle = mod(floor(seconds * 60.0), 64.0) / 63.0 * 6.28318 + UV2.x * 1.2566371;
	float t = (sin(angle) + 1.0) * 0.5;
	vec4 c = UV2.y > 0.5 ? mix(inner_from, inner_to, t) : mix(outer_from, outer_to, t);
	colour = c / 128.0;
	colour.rgb = pow(colour.rgb, vec3(2.2));
}

void fragment() {
	vec4 texel = texture(picture, UV);
	// As the nanotech ring: the alpha share is not linearized, only the hue.
	ALBEDO = texel.rgb * colour.rgb * texel.a * colour.a;
	ALPHA = 1.0;
}
