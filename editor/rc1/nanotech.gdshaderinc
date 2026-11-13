// The nanotech ball as draw_nanotech_ball draws it (effects.py, rc1/nanotech.gd): its sphere
// turned to face the camera (x toward it, z along the crate's up), bobbing, in effect texture 21
// times its vertex colours (0x80 = 1.0). The ring (RING) is its 32 quads around the sphere in
// effect texture 11, whose inner corners' alpha pulses with the bob.

uniform sampler2D picture : source_color, filter_linear, repeat_enable;
uniform vec3 centre;
uniform vec3 up_axis = vec3(0.0, 1.0, 0.0);
uniform float bob_amplitude = 0.06;
uniform float bob_speed = 2.0943951;  // 0.0349 a frame at 60 frames a second
uniform float phase = 0.0;

varying vec4 colour;

void vertex() {
	float bob = sin(TIME * bob_speed + phase);
	vec3 at = centre + up_axis * (0.5 + bob_amplitude * bob);
	vec3 forward = normalize(CAMERA_POSITION_WORLD - at);
	vec3 side = normalize(cross(forward, up_axis));
	vec3 up = cross(side, forward);
	vec3 world = at + VERTEX.x * forward + VERTEX.y * side + VERTEX.z * up;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(world, 1.0);
	colour = vec4(pow(COLOR.rgb * 255.0 / 128.0, vec3(2.2)), COLOR.a * 255.0 / 128.0);
#ifdef RING
	// (pulse * 64 + 128) / 128, pulse = bob / amplitude.
	colour.a *= (bob * 64.0 + 128.0) / 128.0;
#endif
}

void fragment() {
	vec4 texel = texture(picture, UV);
	ALBEDO = texel.rgb * colour.rgb;
	ALPHA = clamp(texel.a * colour.a, 0.0, 1.0);
}
