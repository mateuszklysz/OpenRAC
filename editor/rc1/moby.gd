@tool
extends Node3D
## A moby class scene (mobys/moby_<class>.tscn), on every placed moby of the class.
##
## Light: gives its model the moby's own light, as the game lights it: the ambient colour
## (rc1_colour, red, green, blue bytes; 0x80 = 1.0) and the light word (rc1_light: set, second
## set, fade) that rc1/lit_moby.gdshader reads. A moby without them (one a cutscene adds) keeps
## what the game's CreateMoby gives: set 0 and an ambient of 0x40.
##
## Animation: a placed moby plays the sequence the game starts every moby on (InitMobyInstance:
## the class's sequence 0), looping, instead of standing in its bind pose. In the editor it
## follows the time of the level's cutscene preview, so a background moves with the scene. A
## cutscene's own actor is posed by its actor node instead. Display only.

# =============================================================================
# VARIABLES
# =============================================================================

## The model's animation player, or null for a class without sequences.
var player: AnimationPlayer

## The sequence 0 clip's name in player, or empty.
var clip: StringName = &""

## The cutscene whose preview time the editor follows (found again when it goes).
var cutscene: Node

## The last time the pose was set for, so an unmoved timeline costs nothing.
var shown: float = -1.0

## Frames until the next search for a cutscene.
var search_in: int = 0

# =============================================================================
# CONSTANTS
# =============================================================================

## Frames between searches for a cutscene in the edited scene.
const SEARCH_EVERY: int = 30

# =============================================================================
# METHODS
# =============================================================================

func _ready() -> void:
	apply_light()

	# A cutscene actor's model: the actor poses it.
	if get_parent() != null and get_parent().is_class("CutsceneActor"):
		set_process(false)
		return

	find_clip()

	# A class without sequence 0 stays as it is.
	if clip == &"":
		set_process(false)
		return

	if Engine.is_editor_hint():
		show_at(0.0)
	else:
		player.play(clip)
		set_process(false)


func _process(_delta: float) -> void:
	# The editor only: follow the cutscene preview's time.
	if cutscene == null or not is_instance_valid(cutscene):
		search_in -= 1

		# Searching every frame would cost too much with hundreds of mobys.
		if search_in > 0:
			return

		search_in = SEARCH_EVERY
		cutscene = find_cutscene()

		# No cutscene in the scene: the first frame stays.
		if cutscene == null:
			return

	show_at(cutscene.get_timeline_position())


## Sets the moby's ambient and light word on every mesh of its model.
func apply_light() -> void:
	var colour: Array = get_meta("rc1_colour", [0x40, 0x40, 0x40])
	var word: int = int(get_meta("rc1_light", 0))
	var ambient := Color(colour[0] / 128.0, colour[1] / 128.0, colour[2] / 128.0, 1.0)

	for node in find_children("*", "GeometryInstance3D", true, false):
		var mesh := node as GeometryInstance3D
		mesh.set_instance_shader_parameter("ambient", ambient)
		mesh.set_instance_shader_parameter("light_word", word)


## Finds the model's player and its sequence 0 clip (seq_00, the import may drop a "_loop").
func find_clip() -> void:
	var players: Array[Node] = find_children("*", "AnimationPlayer", true, false)

	# A class without a skeleton has no player.
	if players.is_empty():
		return

	player = players[0] as AnimationPlayer

	for name in player.get_animation_list():
		if name == "seq_00" or name == "seq_00_loop":
			clip = name
			return


## The first cutscene in the edited scene, or null.
func find_cutscene() -> Node:
	var root: Node = get_tree().edited_scene_root if is_inside_tree() else null

	# Outside an edited scene there is no preview to follow.
	if root == null:
		return null

	var found: Array[Node] = root.find_children("*", "Cutscene", true, false)
	return null if found.is_empty() else found[0]


## Poses the model at seconds into its clip, looping.
func show_at(seconds: float) -> void:
	# The same time as last frame: the pose is already there.
	if is_equal_approx(seconds, shown):
		return

	shown = seconds
	var animation: Animation = player.get_animation(clip)
	var at: float = fmod(seconds, animation.length) if animation.length > 0.0 else 0.0

	if player.assigned_animation != clip:
		player.assigned_animation = clip

	player.seek(at, true)
