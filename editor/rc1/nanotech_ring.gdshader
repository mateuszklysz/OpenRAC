// The nanotech ball's ring, added (ALPHA 0x48: Godot's blend_add adds ALBEDO * ALPHA).
shader_type spatial;
render_mode unshaded, cull_disabled, depth_draw_never, blend_add;

#define RING
#include "res://rc1/nanotech.gdshaderinc"
