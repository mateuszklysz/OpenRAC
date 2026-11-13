// The four faint quads on a whole nanotech crate's sides (draw_nanotech_ball, state 1): grey
// 0x7f at alpha 0x10, blended, in effect texture 21 whose coordinates follow the view: the
// direction from the camera to each corner, its x and up parts set to length 2, halved, plus 0.5.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never;

uniform sampler2D picture : source_color, filter_linear, repeat_enable;

varying vec2 slide;

void vertex() {
	vec3 to = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz - CAMERA_POSITION_WORLD;
	// Godot's x and y are the game's x and up.
	slide = normalize(to.xy) + 0.5;
}

void fragment() {
	vec4 texel = texture(picture, slide);
	ALBEDO = texel.rgb * pow(127.0 / 128.0, 2.2);
	ALPHA = texel.a * 16.0 / 128.0;
}
