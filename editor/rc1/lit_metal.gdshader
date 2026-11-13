shader_type spatial;
// A moby's metal ("shine") faces as the game's metal pass draws them (port/viewer/shaders/mesh.vert):
// the level's chrome or glass picture sphere-mapped by the view-space normal, times 0x70, blended
// over the moby at the shine alpha (full up to 16 units nearer than the shine distance, 24, none
// past it), depth tested, not written, pulled a little toward the camera so the moby's own
// surface under it does not hide it. Display only.
render_mode cull_disabled, unshaded, blend_mix, depth_draw_never;

uniform sampler2D metal : source_color, filter_linear_mipmap, repeat_disable;

varying float shine;

void vertex() {
	vec3 e = normalize(MODELVIEW_NORMAL_MATRIX * NORMAL);
	UV = vec2(e.x * 0.5 + 0.5, 0.5 - e.y * 0.5);
	float centre = -(MODELVIEW_MATRIX * vec4(0.0, 0.0, 0.0, 1.0)).z;
	shine = clamp((24.0 - centre) / 16.0, 0.0, 1.0);
	POSITION = PROJECTION_MATRIX * MODELVIEW_MATRIX * vec4(VERTEX, 1.0);
	// Reversed depth: larger is nearer.
	POSITION.z += 1.0e-4 * POSITION.w;
}

void fragment() {
	vec4 texel = texture(metal, UV);
	ALBEDO = texel.rgb * pow(112.0 / 128.0, 2.2);
	ALPHA = clamp(texel.a * shine, 0.0, 1.0);
}
