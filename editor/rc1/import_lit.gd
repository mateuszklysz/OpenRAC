@tool
extends EditorScenePostImport
## Runs when Godot imports one of the level's glTF files (project.godot's scene import default).
## A surface that carries vertex colours shows them: the terrain's are the light the game gives
## it at level load, a moby's its own colours. Godot's glTF import keeps the colours but leaves
## its materials unaware of them. Display only: nothing here runs in the game.

# =============================================================================
# METHODS
# =============================================================================

## The shader that lights a tie or shrub as the game does (one colour per light slot).
const LIT_OBJECT: Shader = preload("res://rc1/lit_object.gdshader")

## The shader that lights a moby as the game does (its light sets and ambient).
const LIT_MOBY: Shader = preload("res://rc1/lit_moby.gdshader")


## Lets every coloured surface under the imported scene use its vertex colours, and gives a tie or
## shrub class the game's lighting of each of its instances.
func _post_import(scene: Node) -> Object:
	var source: String = get_source_file()
	var family: String = source.get_base_dir().get_file()
	var colours_path: String = source.get_base_dir().get_base_dir().path_join(family + "_light.png")
	var lit: bool = family in ["ties", "shrubs"] and ResourceLoader.exists(colours_path)

	for node in scene.find_children("*", "MeshInstance3D", true, false):
		show_vertex_colours((node as MeshInstance3D).mesh, source.get_file() == "terrain.glb")

		# Only a level that was written with its lighting has the colours.
		if lit:
			light_slots((node as MeshInstance3D).mesh, load(colours_path) as Texture2D)

	# A moby, where the level was written with its light sets.
	var bank_path: String = source.get_base_dir().get_base_dir().path_join("light_bank.json")

	if family == "mobys" and FileAccess.file_exists(bank_path):
		var bank: PackedVector4Array = light_bank(bank_path)
		var made: Dictionary = {}

		for node in scene.find_children("*", "MeshInstance3D", true, false):
			light_moby((node as MeshInstance3D).mesh, bank, made)

	return scene


## The level's light sets, 4 rows each (colour A, direction A, colour B, direction B).
func light_bank(path: String) -> PackedVector4Array:
	var rows := PackedVector4Array()
	var data = JSON.parse_string(FileAccess.get_file_as_string(path))

	for light_set in data["sets"]:
		for row in light_set:
			rows.append(Vector4(row[0], row[1], row[2], row[3]))

	return rows


## Gives each textured surface of a moby mesh the lit moby shader (one shader material per
## imported material, in made); the metal (chrome, glass) surfaces keep theirs.
func light_moby(mesh: Mesh, bank: PackedVector4Array, made: Dictionary) -> void:
	# A mesh instance without a mesh has nothing to light.
	if mesh == null:
		return

	for i in mesh.get_surface_count():
		var material := mesh.surface_get_material(i) as BaseMaterial3D

		# Only a textured material that is not the metal pass.
		if material == null or material.albedo_texture == null or material.resource_name.begins_with("metal_"):
			continue

		if not made.has(material):
			var shader := ShaderMaterial.new()
			shader.shader = LIT_MOBY
			var clamped: bool = not material.texture_repeat
			shader.set_shader_parameter("clamped", clamped)
			shader.set_shader_parameter("albedo_clamp" if clamped else "albedo_repeat", material.albedo_texture)
			shader.set_shader_parameter("cutout", material.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
			shader.set_shader_parameter("coloured", material.vertex_color_use_as_albedo)
			shader.set_shader_parameter("bank", bank)
			made[material] = shader

		mesh.surface_set_material(i, made[material])


## Gives each surface of mesh that carries light slots (UV2) the lit object shader, with the
## texture and cutout of its imported material.
func light_slots(mesh: Mesh, colours: Texture2D) -> void:
	# A mesh instance without a mesh has nothing to light.
	if mesh == null:
		return

	for i in mesh.get_surface_count():
		var material := mesh.surface_get_material(i) as BaseMaterial3D
		var slots = mesh.surface_get_arrays(i)[Mesh.ARRAY_TEX_UV2]

		# Surfaces without slots keep their material.
		if material == null or slots == null or slots.size() == 0:
			continue

		var shader := ShaderMaterial.new()
		shader.shader = LIT_OBJECT
		var clamped: bool = not material.texture_repeat
		shader.set_shader_parameter("clamped", clamped)
		shader.set_shader_parameter("albedo_clamp" if clamped else "albedo_repeat", material.albedo_texture)
		shader.set_shader_parameter("cutout", material.transparency == BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR)
		shader.set_shader_parameter("light_colours", colours)
		mesh.surface_set_material(i, shader)


## Turns vertex colours on in the material of each surface of mesh that has them. unshaded: the
## colours are all the light the surface gets (the terrain's, as in the game), not the editor's sun.
func show_vertex_colours(mesh: Mesh, unshaded: bool) -> void:
	# A mesh instance without a mesh has nothing to show.
	if mesh == null:
		return

	for i in mesh.get_surface_count():
		var material := mesh.surface_get_material(i) as BaseMaterial3D
		var colours = mesh.surface_get_arrays(i)[Mesh.ARRAY_COLOR]

		# Only a standard material on a surface with colours; the collision's unlit
		# materials have none, and other materials are left as they are.
		if material != null and colours != null and colours.size() > 0:
			material.vertex_color_use_as_albedo = true

			if unshaded:
				material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
