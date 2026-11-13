shader_type spatial;
// A moby lit as the game lights it (MobyProc and its vertex lighting; port/assets/geometry/
// moby_lighting.cpp): the ambient plus two directional lights of the level's light sets, each
// max(d, d * -|K|) with d the normal's dot with the direction to the light, times the vertex's
// colour multiplier (COLOR), then times the texture. Display only, in floats: close to the game's
// bytes, not bit for bit. Unshaded: these are all the light it gets.
render_mode cull_disabled, unshaded;

uniform sampler2D albedo_repeat : source_color, filter_linear_mipmap, repeat_enable;
uniform sampler2D albedo_clamp : source_color, filter_linear_mipmap, repeat_disable;
uniform bool clamped = false;
uniform bool cutout = false;
uniform bool coloured = false;
// The level's 16 light sets, 4 rows each: colour A, direction A, colour B, direction B (Godot axes;
// a colour's w is the light's back factor K). From light_bank.json (rc1/import_lit.gd).
uniform vec4 bank[64];
// The moby's light word (moby +0x38: set, second set, fade) and ambient (moby +0x3c, / 128).
instance uniform int light_word = 0;
instance uniform vec4 ambient : source_color = vec4(0.5, 0.5, 0.5, 1.0);

varying vec3 light;

vec3 lit(vec3 n) {
	int i0 = light_word & 0xF;
	int i1 = (light_word >> 8) & 0xF;
	int fade = (light_word >> 16) & 0xFF;
	vec4 ca = bank[i0 * 4];
	vec3 da = bank[i0 * 4 + 1].xyz;
	vec4 cb = bank[i0 * 4 + 2];
	vec3 db = bank[i0 * 4 + 3].xyz;
	// A non-zero fade blends the two sets (directions normalised again).
	if (fade != 0) {
		float t = float(fade << 4) / 4096.0;
		ca = mix(ca, bank[i1 * 4], t);
		da = mix(da, bank[i1 * 4 + 1].xyz, t);
		cb = mix(cb, bank[i1 * 4 + 2], t);
		db = mix(db, bank[i1 * 4 + 3].xyz, t);
		da = length(da) > 0.0 ? normalize(da) : da;
		db = length(db) > 0.0 ? normalize(db) : db;
	}
	float a = dot(n, -da);
	float b = dot(n, -db);
	a = max(a, a * -abs(ca.w));
	b = max(b, b * -abs(cb.w));
	return ambient.rgb + ca.rgb * a + cb.rgb * b;
}

void vertex() {
	vec3 world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
	vec3 c = lit(world_normal) * (coloured ? COLOR.rgb : vec3(1.0));
	// The game packs the colour into a byte (at most 255 / 128) and multiplies in its own (gamma)
	// space; Godot in linear light, as the terrain's.
	light = pow(clamp(c, vec3(0.0), vec3(255.0 / 128.0)), vec3(2.2));
}

void fragment() {
	vec4 texel = clamped ? texture(albedo_clamp, UV) : texture(albedo_repeat, UV);
	ALBEDO = texel.rgb * light;
	if (cutout) {
		ALPHA = texel.a;
		ALPHA_SCISSOR_THRESHOLD = 0.5;
	}
}
