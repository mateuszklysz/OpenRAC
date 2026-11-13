@tool
extends Node3D
## The nanotech ball a nanotech crate makes (class 501), which the game's code draws and no class
## model holds (effects.py writes its tables, effects/nanotech.json). rc1/moby.gd adds one to a
## crate, with the crate's look:
##
## - "whole": the ball inside the crate's glass, and four faint quads on the crate's sides whose
##   texture slides with the view,
## - "broken": the crate gone, the ball where it was.
##
## The ball is the half of a sphere turned to the camera and a pulsing ring around it, bobbing.
##
## Its sparks: eight points circling the ball, each leaving a trail of fading particles
## (rc1/nanotech_sparks.gdshader). Everything moves with the cutscene preview's time when there
## is a cutscene, else with the editor's own. Display only: the node is not saved.

# =============================================================================
# CONSTANTS
# =============================================================================

## The ring's inner and outer radius (draw_nanotech_ball: 0.3, times 1.125 outside).
const RING_INNER: float = 0.3
const RING_OUTER: float = 0.3 * 1.125

## The ring's inner corners' colour (0x20, 0x20, 0x80; alpha 0x80, pulsed by the shader).
const RING_COLOUR := Color(0x20 / 255.0, 0x20 / 255.0, 0x80 / 255.0, 0x80 / 255.0)

const BALL_SHADER: Shader = preload("res://rc1/nanotech_ball.gdshader")
const RING_SHADER: Shader = preload("res://rc1/nanotech_ring.gdshader")
const SHIMMER_SHADER: Shader = preload("res://rc1/nanotech_shimmer.gdshader")
const SPARKS_SHADER: Shader = preload("res://rc1/nanotech_sparks.gdshader")

## The sparks' longest trail, in frames, and their distance from the ball's centre.
const TRAIL: int = 40
const SPARK_RADIUS: float = 0.25

## Frames between searches for a cutscene.
const SEARCH_EVERY: int = 30

# =============================================================================
# VARIABLES
# =============================================================================

## The crate it belongs to.
var crate: Node3D

## The look shown: "whole" or "broken".
var look: String = "whole"

## The ball's and ring's materials, whose centre follows the crate.
var materials: Array[ShaderMaterial] = []

## The time when there is no cutscene to follow.
var own_seconds: float = 0.0

## The cutscene whose preview time the effect follows, and the frames until the next search.
var cutscene: Node
var search_in: int = 0

## The tables of every level read so far, by their file.
static var tables_of: Dictionary = {}

# =============================================================================
# METHODS
# =============================================================================

## Adds the effect of the crate's look to it, or nothing when its level has no tables.
static func attach(to: Node3D, effects_dir: String, shown: String) -> Node3D:
	var tables: Dictionary = read_tables(effects_dir)

	if tables.is_empty():
		return null

	var effect := new()
	effect.name = "NanotechEffect"
	effect.crate = to
	effect.look = shown
	effect.top_level = true
	to.add_child(effect)
	effect.build(tables, effects_dir)
	return effect


## effects/nanotech.json, read once per level; empty when it is not there.
static func read_tables(effects_dir: String) -> Dictionary:
	var path: String = effects_dir.path_join("nanotech.json")

	if tables_of.has(path):
		return tables_of[path]

	var found: Dictionary = {}

	if FileAccess.file_exists(path):
		var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))

		if parsed is Dictionary:
			found = parsed

	tables_of[path] = found
	return found


func _process(delta: float) -> void:
	follow()
	own_seconds += delta
	var seconds: float = own_seconds

	# The level's cutscene preview: its time, when it has one. A crate without sequence 0 does
	# not look for it itself, so the effect does (rc1/moby.gd's search, now and then).
	if cutscene == null or not is_instance_valid(cutscene):
		cutscene = null
		search_in -= 1

		if search_in <= 0 and is_instance_valid(crate) and crate.has_method("find_cutscene"):
			search_in = SEARCH_EVERY
			cutscene = crate.find_cutscene()

	if cutscene != null:
		seconds = cutscene.get_timeline_position()

	for material in materials:
		material.set_shader_parameter("seconds", seconds)


## Puts the effect on the crate: its place and turn, without its scale (the game draws it at
## the moby's position with the rows of its rotation).
func follow() -> void:
	if crate == null or not is_instance_valid(crate):
		return

	global_transform = crate.global_transform.orthonormalized()

	# The game's up is the crate's z.
	var up: Vector3 = (crate.global_basis * Vector3(0, 0, 1)).normalized()

	for material in materials:
		material.set_shader_parameter("centre", crate.global_position)
		material.set_shader_parameter("up_axis", up)


func build(tables: Dictionary, effects_dir: String) -> void:
	var textures: Dictionary = tables.get("textures", {})
	var ball_picture: Texture2D = load_picture(effects_dir.path_join(textures.get("ball", "")))
	var ring_picture: Texture2D = load_picture(effects_dir.path_join(textures.get("ring", "")))
	var phase: float = randf() * PI

	add_mesh("Ball", ball_mesh(tables["ball"]), BALL_SHADER, ball_picture, phase, true)
	add_mesh("Ring", ring_mesh(tables["ring"]), RING_SHADER, ring_picture, phase, true)

	if look == "whole":
		add_mesh("Shimmer", shimmer_mesh(tables["shimmer"]), SHIMMER_SHADER, ball_picture, phase, false)

	var spark_picture: Texture2D = load_picture(effects_dir.path_join(textures.get("spark", "")))

	if spark_picture != null:
		add_mesh("Sparks", sparks_mesh(), SPARKS_SHADER, spark_picture, phase, true)
		# Drawn after the sphere, which writes no depth: inside it, as in the game.
		materials[-1].render_priority = 1
		set_sparks(materials[-1])

	follow()


## The sparks' places when they start (nanotech_ball_update, state 0): along the crate's rows
## and between them, each turning about an axis across it; and the ball's own random turn of
## all eight (random direction, random angle of 45 to 90 degrees either way).
func set_sparks(material: ShaderMaterial) -> void:
	var b: Basis = crate.global_basis.orthonormalized()
	var r0: Vector3 = b * Vector3(1, 0, 0)
	var r1: Vector3 = b * Vector3(0, 1, 0)
	var r2: Vector3 = b * Vector3(0, 0, 1)
	var offsets: Array[Vector3] = [r0, r1, r2, (r0 + r2).normalized(), (r0 + r1).normalized(),
		(r1 - r2).normalized(), (r0 - r2).normalized(), (r0 - r1).normalized()]
	var axes: Array[Vector3] = [r2, r0, r1, offsets[3].cross(r1), offsets[4].cross(r2),
		offsets[5].cross(r0), offsets[6].cross(r1), offsets[7].cross(r2)]
	var random := RandomNumberGenerator.new()
	random.seed = int(crate.get_meta("rc1_index", 0))
	var around: float = random.randf_range(0.0, TAU)
	var tilt: float = random.randf_range(-PI / 2, PI / 2)
	var turn_axis := Vector3(cos(tilt) * cos(around), sin(tilt), cos(tilt) * sin(around))
	var turn_angle: float = random.randf_range(PI / 4, PI / 2) * (1.0 if random.randf() < 0.5 else -1.0)

	material.set_shader_parameter("offsets", PackedVector3Array(offsets.map(func(v: Vector3) -> Vector3: return v * SPARK_RADIUS)))
	material.set_shader_parameter("axes", PackedVector3Array(axes.map(func(v: Vector3) -> Vector3: return v.normalized())))
	material.set_shader_parameter("turn_axis", turn_axis)
	material.set_shader_parameter("turn_angle", turn_angle)


func add_mesh(called: String, mesh: ArrayMesh, shader: Shader, picture: Texture2D, phase: float,
		billboard: bool) -> void:
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("picture", picture)

	if billboard:
		material.set_shader_parameter("phase", phase)
		materials.append(material)

	var node := MeshInstance3D.new()
	node.name = called
	node.mesh = mesh
	node.material_override = material
	node.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF

	# The billboards move in the shader: never culled by their mesh's bounds.
	if billboard:
		node.extra_cull_margin = 16.0

	add_child(node)


## A PNG of the project read as it is, without an import.
static func load_picture(path: String) -> Texture2D:
	var image := Image.load_from_file(ProjectSettings.globalize_path(path)) if FileAccess.file_exists(path) else null

	if image == null:
		return null

	return ImageTexture.create_from_image(image)


## The sphere: its strips as triangles, in the camera-facing frame the shader turns.
static func ball_mesh(ball: Dictionary) -> ArrayMesh:
	var vertices := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colours := PackedColorArray()

	for strip in ball["strips"]:
		var first: int = int(strip[0])

		for i in range(int(strip[1]) - 2):
			for k in [0, 1, 2]:
				var at: int = first + i + k
				var p: Array = ball["vertices"][at]
				var uv: Array = ball["uvs"][at]
				var c: Array = ball["colours"][at]
				vertices.append(Vector3(p[0], p[1], p[2]))
				uvs.append(Vector2(uv[0], uv[1]))
				colours.append(Color(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0, c[3] / 255.0))

	return triangles(vertices, uvs, colours)


## The ring: each quad's first two corners on the inner circle (centre of effect texture 11,
## the colour), its last two on the outer one (edge of the texture, clear).
static func ring_mesh(ring: Dictionary) -> ArrayMesh:
	var vertices := PackedVector3Array()
	var uvs := PackedVector2Array()
	var colours := PackedColorArray()

	for quad in ring["quads"]:
		var corners: Array[Vector3] = []

		for j in range(4):
			var p: Array = ring["vertices"][int(quad[j])]
			var radius: float = RING_INNER if j < 2 else RING_OUTER
			corners.append(Vector3(p[0], p[1], p[2]) * radius)

		for k in [0, 1, 2, 1, 3, 2]:
			vertices.append(corners[k])
			uvs.append(Vector2(0.5, 0.5 if k < 2 else 1.0))
			colours.append(RING_COLOUR if k < 2 else Color(0, 0, 0, 0))

	return triangles(vertices, uvs, colours)


## The sparks' particles: a quad per spark and age (0 to TRAIL - 1 frames), the spark and age
## in CUSTOM0 for the shader to place.
static func sparks_mesh() -> ArrayMesh:
	var vertices := PackedVector3Array()
	var uvs := PackedVector2Array()
	var custom := PackedFloat32Array()

	for spark in range(8):
		for age in range(TRAIL):
			for corner in [Vector2(-1, -1), Vector2(1, -1), Vector2(1, 1), Vector2(-1, -1), Vector2(1, 1), Vector2(-1, 1)]:
				vertices.append(Vector3(corner.x, corner.y, 0))
				uvs.append(Vector2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5))
				custom.append_array([spark, age, 0, 0])

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_TEX_UV] = uvs
	arrays[Mesh.ARRAY_CUSTOM0] = custom
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays, [],  {},
		Mesh.ARRAY_CUSTOM_RGBA_FLOAT << Mesh.ARRAY_FORMAT_CUSTOM0_SHIFT)
	return mesh


## The shimmer's four quads on the crate's sides, in the crate's frame.
static func shimmer_mesh(shimmer: Dictionary) -> ArrayMesh:
	var vertices := PackedVector3Array()

	for quad in shimmer["quads"]:
		for k in [0, 1, 2, 1, 3, 2]:
			var p: Array = shimmer["vertices"][int(quad[k])]
			vertices.append(Vector3(p[0], p[1], p[2]))

	return triangles(vertices, PackedVector2Array(), PackedColorArray())


static func triangles(vertices: PackedVector3Array, uvs: PackedVector2Array,
		colours: PackedColorArray) -> ArrayMesh:
	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices

	if not uvs.is_empty():
		arrays[Mesh.ARRAY_TEX_UV] = uvs

	if not colours.is_empty():
		arrays[Mesh.ARRAY_COLOR] = colours

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	return mesh
