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
##
## Start state: what the game does with a placed moby when the level starts, which its record
## alone does not show. The game's own code works it out: moby_states.py runs the level for a
## few seconds and writes moby_states.json beside level.json (a moby missing from it is not
## there, "hidden" one the game does not draw). Without that file, and for the reasons shown,
## its spawn flags (spawned only before or only after a mission of the planet is done) and its
## class's rule (rc1/class_rules.json: a class whose own code hides it,
## or which the game's code draws more of, as the nanotech crate's ball, rc1/nanotech.gd). The
## Inspector shows it read-only, with its reason. A moby the game keeps hidden is not drawn, or
## drawn faint with the project setting openrac/editor/show_hidden_mobys. Display only: nothing
## of it is saved.

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

## The class rules (rc1/class_rules.json).
const RULES_PATH: String = "res://rc1/class_rules.json"

## The project setting that draws hidden mobys faint instead of not at all.
const SHOW_HIDDEN: String = "openrac/editor/show_hidden_mobys"

## How faint a hidden moby is drawn with SHOW_HIDDEN.
const GHOST: float = 0.75

## Spawn flag bits of a level moby record (load_level_gameplay): spawned while its mission is not
## done (1), once it is (2); gone for good once killed or picked up (8, or 4 without 8).
const SPAWN_BEFORE: int = 1
const SPAWN_AFTER: int = 2
const SPAWN_SAVED: int = 4
const SPAWN_ONCE: int = 8

# =============================================================================
# START STATE
# =============================================================================

## Whether the game keeps it hidden when the level starts.
var hidden_at_start: bool = false

## Whether that depends on the player's progress or on what happens in the level.
var depends_on_game: bool = false

## Why, one line per reason.
var start_reasons: PackedStringArray = []

## The class's looks (class_rules.json "looks"), and the one shown.
var looks: PackedStringArray = []
var look: String = ""

## The class rules, read once.
static var rules: Dictionary = {}

## Each level folder's moby_states.json: {"time": modified time, "mobys": {key: moby}}.
static var level_states: Dictionary = {}

## The level folders the states program was run for this session (run once each).
static var states_tried: Dictionary = {}

## The game's state of this moby when the level starts (moby_states.json), or empty.
var game_state: Dictionary = {}

# =============================================================================
# METHODS
# =============================================================================

func _ready() -> void:
	apply_light()
	decide_start()
	show_start()

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


func _get_property_list() -> Array[Dictionary]:
	# A moby the level places: its start state, read-only, and its class's looks.
	if not has_meta("rc1_index"):
		return []

	var properties: Array[Dictionary] = [
		{"name": "Start state", "type": TYPE_NIL, "usage": PROPERTY_USAGE_GROUP},
		{"name": "start_state", "type": TYPE_STRING, "hint": PROPERTY_HINT_MULTILINE_TEXT,
			"usage": PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY},
	]

	if not looks.is_empty():
		properties.append({"name": "preview_look", "type": TYPE_STRING, "hint": PROPERTY_HINT_ENUM,
			"hint_string": ",".join(looks), "usage": PROPERTY_USAGE_EDITOR})

	return properties


func _get(property: StringName) -> Variant:
	if property == &"start_state":
		var head: String = "Hidden" if hidden_at_start else "Shown"

		if depends_on_game:
			head += " (depends on the game)"

		if not game_state.is_empty():
			head += ", state %d" % int(game_state["state"])

		return head + ("\n" + "\n".join(start_reasons) if not start_reasons.is_empty() else "")

	if property == &"preview_look":
		return look

	return null


func _set(property: StringName, value: Variant) -> bool:
	if property != &"preview_look" or not String(value) in looks:
		return false

	look = String(value)
	show_start()
	return true


## The class rules (rc1/class_rules.json "classes"), read once.
static func class_rules() -> Dictionary:
	# The setting shows in Project Settings (Advanced) once a moby is read; reopen the scene
	# after changing it.
	if not ProjectSettings.has_setting(SHOW_HIDDEN):
		ProjectSettings.set_setting(SHOW_HIDDEN, false)
		ProjectSettings.set_initial_value(SHOW_HIDDEN, false)
		ProjectSettings.add_property_info({"name": SHOW_HIDDEN, "type": TYPE_BOOL})

	if rules.is_empty() and FileAccess.file_exists(RULES_PATH):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(RULES_PATH))
		rules = parsed.get("classes", {}) if parsed is Dictionary else {}

	return rules


## Works out the start state of a moby the level places.
func decide_start() -> void:
	if not has_meta("rc1_index"):
		return

	var flags: int = int(get_meta("rc1_spawn_flags", 0))
	var mission: int = int(get_meta("rc1_unknown_4", -1))

	if flags & (SPAWN_BEFORE | SPAWN_AFTER) == SPAWN_AFTER:
		hidden_at_start = true
		depends_on_game = true
		start_reasons.append("Spawned only once mission %d of this planet is done (spawn flags %d): not in a new game." % [mission, flags])
	elif flags & (SPAWN_BEFORE | SPAWN_AFTER) == SPAWN_BEFORE:
		depends_on_game = true
		start_reasons.append("Gone once mission %d of this planet is done (spawn flags %d)." % [mission, flags])

	if flags & SPAWN_ONCE or flags & (SPAWN_SAVED | SPAWN_ONCE) == SPAWN_SAVED:
		depends_on_game = true
		start_reasons.append("Gone for good once Ratchet kills, breaks or picks it up: the save keeps it.")

	var rule: Dictionary = class_rules().get(str(int(get_meta("rc1_class", -1))), {})

	var states: Variant = game_states()

	# The game's own run, when there is one; the class rules only explain it.
	if states != null:
		decide_by_game(states, rule)
	elif rule.has("hidden_unless_pvar"):
		decide_by_pvars(rule)

	if rule.has("progress"):
		depends_on_game = true
		start_reasons.append(String(rule["progress"]))

	if rule.has("source"):
		start_reasons.append("Rule from " + String(rule["source"]) + ".")

	looks = PackedStringArray(rule.get("looks", []))
	look = looks[0] if not looks.is_empty() else ""


## The level's moby states from the game's run (moby_states.json), read again when the file
## changes, or null without one.
func game_states() -> Variant:
	var path: String = scene_file_path.get_base_dir().get_base_dir().path_join("moby_states.json")

	if not FileAccess.file_exists(path) and not states_tried.has(path):
		states_tried[path] = true
		run_states(path)

	if not FileAccess.file_exists(path):
		return null

	var time: int = FileAccess.get_modified_time(path)
	var known: Dictionary = level_states.get(path, {})

	if known.get("time", -1) != time:
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
		var mobys: Dictionary = {}

		if parsed is Dictionary:
			for moby in parsed.get("mobys", []):
				mobys[int(moby["key"])] = moby

		known = {"time": time, "mobys": mobys, "ticks": int(parsed.get("ticks", 0)) if parsed is Dictionary else 0}
		level_states[path] = known

	return known


## Runs the port's states program (moby_states.py does the same from a shell) for the level
## whose moby_states.json is PATH, when the program, the game's data and the port's levels are
## found: the data from the editor's command line (--openrac-data DIR after --, as the launcher
## opens a level) or OPENRAC_DATA, the rest from the checkout the project sits in
## (assets/levels/rac1/<version>/level_NN).
static func run_states(path: String) -> void:
	var folder: String = ProjectSettings.globalize_path(path.get_base_dir())
	var stats: Variant = JSON.parse_string(FileAccess.get_file_as_string(folder.path_join("level.json")))

	if not stats is Dictionary:
		return

	var version: String = String(stats.get("version", "ntsc"))
	var root: String = ProjectSettings.globalize_path("res://").path_join("../../../../..").simplify_path()
	var program: String = root.path_join("port/build/release/openrac-rac1-%s-states" % version)

	if OS.get_name() == "Windows":
		program += ".exe"

	var data: String = OS.get_environment("OPENRAC_DATA")
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var at: int = args.find("--openrac-data")

	if at >= 0 and at + 1 < args.size():
		data = args[at + 1]

	var levels: String = root.path_join("assets/port/rac1").path_join(version)

	if data == "" or not FileAccess.file_exists(program) or not DirAccess.dir_exists_absolute(levels):
		return

	var output: Array = []
	var code: int = OS.execute(program, ["--data", data, "--levels", levels, "--level", str(int(stats["level"])),
		"--moby-states", ProjectSettings.globalize_path(path)], output, true)

	if code != 0:
		push_warning("The moby states program failed for %s: %s" % [path, "".join(output).right(400)])


## The start state the game's run gave.
func decide_by_game(states: Dictionary, rule: Dictionary) -> void:
	var seconds: float = states["ticks"] / 60.0
	game_state = states["mobys"].get(int(get_meta("rc1_index")), {})

	if game_state.is_empty():
		hidden_at_start = true
		start_reasons.append("Not in the level %.0f s after it starts (the game's run, moby_states.json)." % seconds)
	elif int(game_state["dead"]) != 0:
		hidden_at_start = true
		start_reasons.append("Dead %.0f s after the level starts (the game's run, moby_states.json)." % seconds)
	elif int(game_state["hidden"]) != 0 and rule.has("game_hidden"):
		start_reasons.append(String(rule["game_hidden"]))
	elif int(game_state["hidden"]) != 0:
		hidden_at_start = true
		start_reasons.append("Not drawn %.0f s after the level starts (the game's run, moby_states.json)." % seconds)


## A class whose code hides the moby unless one of some pvar words is set (amoeboids: state 12,
## or 13 until Ratchet enters the volume another word names).
func decide_by_pvars(rule: Dictionary) -> void:
	for offset in rule["hidden_unless_pvar"]:
		if pvar_word(String(offset).hex_to_int()) != 0:
			return

	hidden_at_start = true

	if not rule.has("trigger_volume_pvar"):
		start_reasons.append("Hidden by its class's code.")
		return

	var volume: int = int(pvar_word(String(rule["trigger_volume_pvar"]).hex_to_int()))

	if volume == -1:
		start_reasons.append("Hidden by its class's code, and nothing in its data brings it out.")
	else:
		depends_on_game = true
		start_reasons.append("Hidden until Ratchet enters volume %d." % volume)


## The pvar word at a byte offset (0 when it has none).
func pvar_word(offset: int) -> Variant:
	var words: Array = get_meta("rc1_pvar", [])
	var at: int = offset / 4
	return words[at] if at < words.size() else 0


## Draws the moby as it starts: hidden (or faint), in its look, with the code-drawn effect of
## its class.
func show_start() -> void:
	if not has_meta("rc1_index"):
		return

	var effect: Node = get_node_or_null("NanotechEffect")

	if effect != null:
		remove_child(effect)
		effect.queue_free()

	var rule: Dictionary = class_rules().get(str(int(get_meta("rc1_class", -1))), {})
	var gone: bool = hidden_at_start or look == "broken"

	if rule.get("effect", "") == "nanotech" and not hidden_at_start:
		var directory: String = scene_file_path.get_base_dir().get_base_dir().path_join("effects")
		var Nanotech: Script = load("res://rc1/nanotech.gd")
		Nanotech.attach(self, directory, look)

	var ghost: bool = ProjectSettings.get_setting(SHOW_HIDDEN, false)

	for node in find_children("*", "GeometryInstance3D", true, false):
		# The effect is drawn whole in its look.
		if node.get_parent() != null and node.get_parent().name == "NanotechEffect":
			continue

		var instance: RID = (node as GeometryInstance3D).get_instance()
		RenderingServer.instance_set_visible(instance, (node as Node3D).is_visible_in_tree() and (not gone or ghost))
		RenderingServer.instance_geometry_set_transparency(instance, GHOST if gone and ghost else 0.0)
