@tool
extends Node3D
## A moby class scene (mobys/moby_<class>.tscn). Gives its model the moby's own light, as the game
## lights it: the ambient colour (rc1_colour, red, green, blue bytes; 0x80 = 1.0) and the light
## word (rc1_light: set, second set, fade) that rc1/lit_moby.gdshader reads. A moby without them
## (one a cutscene adds) keeps what the game's CreateMoby gives: set 0 and an ambient of 0x40.
## Display only.

# =============================================================================
# METHODS
# =============================================================================

func _ready() -> void:
	apply_light()


## Sets the moby's ambient and light word on every mesh of its model.
func apply_light() -> void:
	var colour: Array = get_meta("rc1_colour", [0x40, 0x40, 0x40])
	var word: int = int(get_meta("rc1_light", 0))
	var ambient := Color(colour[0] / 128.0, colour[1] / 128.0, colour[2] / 128.0, 1.0)

	for node in find_children("*", "GeometryInstance3D", true, false):
		var mesh := node as GeometryInstance3D
		mesh.set_instance_shader_parameter("ambient", ambient)
		mesh.set_instance_shader_parameter("light_word", word)
