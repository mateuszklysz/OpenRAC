// The nanotech ball's sparks (rc1/nanotech.gd): eight points circling the ball, each about an
// axis of its own (0.0698 rad a frame), turned together by the ball's own random turn, and each
// leaving a particle every frame where it is, which fades by 127 / life a frame (life 40 frames
// while the camera is within 10 units, else 5). A quad of this mesh is one such particle: its
// spark (CUSTOM0.x) and its age in frames (CUSTOM0.y). Added (ALPHA 0x48), colour 0x40, 0x40,
// 0x7f, a sprite of half-diagonal 10000 / 210000 units facing the camera.
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, blend_add;

uniform sampler2D picture : filter_linear;
uniform vec3 centre;
uniform vec3 up_axis = vec3(0.0, 1.0, 0.0);
uniform vec3 offsets[8];
uniform vec3 axes[8];
uniform vec3 turn_axis = vec3(0.0, 1.0, 0.0);
uniform float turn_angle = 0.0;
uniform float seconds = 0.0;
uniform float phase = 0.0;
uniform float bob_amplitude = 0.06;
uniform float bob_speed = 2.0943951;
uniform float spin_speed = 4.1887902;  // 0.0698 rad a frame at 60 frames a second

const float HALF = 10000.0 / 210000.0 * 0.70710678;

varying float alpha;

vec3 turned(vec3 v, vec3 k, float a) {
	return v * cos(a) + cross(k, v) * sin(a) + k * dot(k, v) * (1.0 - cos(a));
}

void vertex() {
	int spark = int(CUSTOM0.x + 0.5);
	float age = CUSTOM0.y;
	// The game's x and y are Godot's x and z.
	float life = distance(CAMERA_POSITION_WORLD.xz, centre.xz) < 10.0 ? 40.0 : 5.0;
	alpha = (127.0 - age * floor(127.0 / life)) / 128.0;
	float t = seconds - age / 60.0;
	vec3 offset = turned(turned(offsets[spark], axes[spark], spin_speed * t), turn_axis, turn_angle);
	vec3 at = centre + up_axis * (0.5 + bob_amplitude * sin(bob_speed * t + phase)) + offset;
	vec3 right = INV_VIEW_MATRIX[0].xyz;
	vec3 up = INV_VIEW_MATRIX[1].xyz;
	vec3 world = at + (right * VERTEX.x + up * VERTEX.y) * HALF;
	POSITION = PROJECTION_MATRIX * VIEW_MATRIX * vec4(world, 1.0);
	// Past its life: nothing drawn.
	if (age >= life || alpha <= 0.0) {
		POSITION = vec4(0.0, 0.0, 2.0, 1.0);
	}
}

void fragment() {
	vec4 texel = texture(picture, UV);
	// The GS adds in display colour: each particle's share is added as it is, not linearized,
	// else the trail is far fainter than on the PS2.
	ALBEDO = texel.rgb * vec3(64.0, 64.0, 127.0) / 128.0;
	ALPHA = texel.a * alpha;
}
