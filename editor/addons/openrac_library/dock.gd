@tool
extends EditorDock
## The Assets dock: the mobys, ties and shrubs of every level of every disc read (library.gd), to
## drag onto the level or add in front of the 3D view's camera (plugin.gd places them). Each
## shows what it does here: green for a class the level has (the game's own, with its logic),
## white for the rest (its look only, as static scenery, for now).

const Library = preload("library.gd")
const AssetList = preload("asset_list.gd")

## The filter's choices.
const SHOW_ALL: int = 0
const SHOW_HERE: int = 1
const SHOW_THIS_GAME: int = 2
const SHOW_OTHER_GAMES: int = 3

## The names of assets this level has (placed with the game's logic).
const FULL_COLOUR: Color = Color(0.55, 0.9, 0.55)

## Previews made at most this many at once, the rest as the list is scrolled to.
const PREVIEWS_PER_FRAME: int = 2

var plugin: EditorPlugin

## The level the edited scene is (library.gd here_of()), or empty.
var here: Dictionary = {}

## Every level found: library.gd levels_in()'s entries.
var levels: Array = []

## Each level's assets once read, by its folder.
var assets_by_level: Dictionary = {}

## The assets the list shows, in its order.
var shown: Array = []

## Previews already made, by model file.
var previews: Dictionary = {}

## Models still to preview: [model file].
var preview_queue: Array = []

var search: LineEdit
var filter: OptionButton
var tree: Tree
var list: AssetList
var report: Label
var add_button: Button


func _init() -> void:
	title = "Assets"
	default_slot = EditorDock.DOCK_SLOT_LEFT_BR
	var box := VBoxContainer.new()
	box.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(box)

	var row := HBoxContainer.new()
	box.add_child(row)
	search = LineEdit.new()
	search.placeholder_text = "Search: name or class number"
	search.clear_button_enabled = true
	search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	search.text_changed.connect(func(_text: String) -> void: refill())
	row.add_child(search)
	var refresh := Button.new()
	refresh.text = "Refresh"
	refresh.tooltip_text = "Look for levels again (after reading another disc)."
	refresh.pressed.connect(rescan)
	row.add_child(refresh)

	filter = OptionButton.new()
	filter.add_item("Everything", SHOW_ALL)
	filter.add_item("In this level (with the game's logic)", SHOW_HERE)
	filter.add_item("This game", SHOW_THIS_GAME)
	filter.add_item("Other games", SHOW_OTHER_GAMES)
	filter.item_selected.connect(func(_index: int) -> void: refill())
	box.add_child(filter)
	var legend := Label.new()
	legend.text = "Green: this level has it (the game's logic). White: its look only, for now."
	legend.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	legend.custom_minimum_size = Vector2(160, 0)
	legend.add_theme_color_override("font_color", FULL_COLOUR.darkened(0.1))
	box.add_child(legend)

	var split := VSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	box.add_child(split)
	tree = Tree.new()
	tree.hide_root = true
	tree.custom_minimum_size = Vector2(0, 120)
	tree.item_selected.connect(refill)
	tree.item_collapsed.connect(func(_item: TreeItem) -> void: pass)
	split.add_child(tree)

	list = AssetList.new()
	list.dock = self
	list.size_flags_vertical = Control.SIZE_EXPAND_FILL
	list.custom_minimum_size = Vector2(0, 200)
	list.icon_mode = ItemList.ICON_MODE_TOP
	list.max_columns = 0
	list.same_column_width = true
	list.fixed_column_width = 96
	list.fixed_icon_size = Vector2i(80, 80)
	list.item_activated.connect(func(index: int) -> void: add(index))
	list.item_selected.connect(func(_index: int) -> void: add_button.disabled = false)
	split.add_child(list)

	add_button = Button.new()
	add_button.text = "Add in front of the camera"
	add_button.tooltip_text = "Or drag it into the 3D view or onto the Scene dock. Double-click adds too."
	add_button.disabled = true
	add_button.pressed.connect(func() -> void:
		var selected: PackedInt32Array = list.get_selected_items()
		if not selected.is_empty():
			add(selected[0]))
	box.add_child(add_button)

	report = Label.new()
	report.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	report.custom_minimum_size = Vector2(160, 0)
	box.add_child(report)


func _ready() -> void:
	rescan()


func _process(_delta: float) -> void:
	for _i in range(PREVIEWS_PER_FRAME):
		if preview_queue.is_empty():
			return

		make_preview(preview_queue.pop_front())


## The edited scene's level changed.
func set_here(level: Dictionary) -> void:
	here = level
	refill()


func show_report(text: String) -> void:
	report.text = text


# =============================================================================
# THE LEVELS
# =============================================================================

## Finds the levels again and lists them by game, disc and level.
func rescan() -> void:
	levels.clear()
	assets_by_level.clear()
	var folders: Dictionary = Library.game_folders(ProjectSettings.globalize_path("res://"), Library.extra_folders())

	for game in folders:
		for folder in folders[game]:
			levels.append_array(Library.levels_in(game, folder))

	tree.clear()
	var root: TreeItem = tree.create_item()
	var items: Dictionary = {}

	for level in levels:
		var game_key: String = String(level["game"])
		var disc_key: String = game_key + "/" + String(level["disc"])

		if not items.has(game_key):
			var game_item: TreeItem = tree.create_item(root)
			game_item.set_text(0, game_key)
			game_item.set_metadata(0, {"game": game_key})
			items[game_key] = game_item

		if not items.has(disc_key):
			var disc_item: TreeItem = tree.create_item(items[game_key])
			disc_item.set_text(0, String(level["disc"]))
			disc_item.set_metadata(0, {"game": game_key, "disc": level["disc"]})
			disc_item.collapsed = true
			items[disc_key] = disc_item

		var level_item: TreeItem = tree.create_item(items[disc_key])
		var name: String = String(level["name"])
		level_item.set_text(0, "Level %02d%s" % [int(level["level"]), "" if name.is_empty() else ": " + name])
		level_item.set_metadata(0, {"level": level})

		for kind in Library.KINDS:
			var kind_item: TreeItem = tree.create_item(level_item)
			kind_item.set_text(0, kind.capitalize())
			kind_item.set_metadata(0, {"level": level, "kind": kind})

		level_item.collapsed = true

	show_report("Levels found: %d, in games: %d." % [levels.size(), folders.size()] if not levels.is_empty()
		else "No levels found. Read a disc's levels from the launcher's Level editor tab first.")
	refill()


## The assets of LEVEL, read once.
func assets_of(level: Dictionary) -> Array:
	var key: String = String(level["dir"])

	if not assets_by_level.has(key):
		assets_by_level[key] = Library.assets_of(level)

	return assets_by_level[key]


# =============================================================================
# THE LIST
# =============================================================================

## Fills the list from the tree's selection, the search and the filter. With nothing selected, a
## search looks in every level (read as it goes); without a search, the edited level is shown.
func refill() -> void:
	if list == null:
		return

	list.clear()
	shown.clear()
	preview_queue.clear()
	add_button.disabled = true
	var selected: TreeItem = tree.get_selected()
	var scope: Dictionary = selected.get_metadata(0) if selected != null else {}
	var words: String = search.text.strip_edges().to_lower()
	var candidates: Array = []

	for level in levels:
		if scope.has("level") and scope["level"]["dir"] != level["dir"]:
			continue

		if scope.has("game") and level["game"] != scope["game"]:
			continue

		if scope.has("disc") and level["disc"] != scope["disc"]:
			continue

		# Nothing chosen and no search: this level's own.
		if scope.is_empty() and words.is_empty() and not is_here(level):
			continue

		for asset in assets_of(level):
			if scope.has("kind") and asset["kind"] != scope["kind"]:
				continue

			candidates.append(asset)

	var seen: Dictionary = {}

	for asset in candidates:
		if not words.is_empty() and not String(asset["name"]).to_lower().contains(words) and str(asset["class"]) != words:
			continue

		var status: String = Library.status(asset, here)

		match filter.get_selected_id():
			SHOW_HERE:
				if status != Library.FULL:
					continue
			SHOW_THIS_GAME:
				if here.is_empty() or asset["game"] != here["game"]:
					continue
			SHOW_OTHER_GAMES:
				if not here.is_empty() and asset["game"] == here["game"]:
					continue

		# A class shown once per game unless a level was chosen: the first level that has it.
		var key: String = "%s/%s/%d" % [asset["game"], asset["kind"], int(asset["class"])]

		if not scope.has("level") and seen.has(key):
			continue

		seen[key] = true
		var index: int = list.add_item(String(asset["name"]))

		if status == Library.FULL:
			list.set_item_custom_fg_color(index, FULL_COLOUR)

		list.set_item_tooltip(index, tooltip(asset, status))
		list.set_item_metadata(index, shown.size())
		shown.append(asset)

		if previews.has(asset["model"]):
			list.set_item_icon(index, previews[asset["model"]])
		elif preview_queue.size() < 200:
			preview_queue.append(asset["model"])


func is_here(level: Dictionary) -> bool:
	return not here.is_empty() and level["game"] == here["game"] and level["disc"] == here["disc"] \
		and int(level["level"]) == int(here["level"])


func tooltip(asset: Dictionary, status: String) -> String:
	var where: String = "%s %s, level %02d%s" % [asset["game"], asset["disc"], int(asset["level"]),
		"" if String(asset["level_name"]).is_empty() else " (%s)" % asset["level_name"]]
	var does: String = ("This level has the class: it is placed as the game's own %s, with its logic." % asset["kind"].trim_suffix("s")
		if status == Library.FULL else
		"Placed as static scenery (Game/StaticAssets): its look, no logic yet; collision with rc1_static_collision.")
	return "%s, class %d\nFrom %s\n%s\nDrag it into the 3D view, or double-click to add it in front of the camera." % [
		asset["name"], int(asset["class"]), where, does]


## The asset of the list's item INDEX.
func asset_at(index: int) -> Dictionary:
	var at: Variant = list.get_item_metadata(index)
	return shown[int(at)] if at != null and int(at) < shown.size() else {}


func add(index: int) -> void:
	var asset: Dictionary = asset_at(index)

	if not asset.is_empty():
		plugin.add_in_view(asset)


# =============================================================================
# PREVIEWS
# =============================================================================

## A preview of the model: its first mesh, read straight from its file (it need not be in this
## project), drawn by the editor's mesh previewer.
func make_preview(model: String) -> void:
	if previews.has(model):
		return

	# Its textures go in as data, so the editor does not try to import files outside the project.
	var glb: Dictionary = Library.read_glb(FileAccess.get_file_as_bytes(model))

	if glb.is_empty():
		return

	for image in glb["json"].get("images", []):
		var uri: String = String(image.get("uri", ""))

		if not uri.is_empty() and not uri.begins_with("data:"):
			var bytes: PackedByteArray = FileAccess.get_file_as_bytes(model.get_base_dir().path_join(uri).simplify_path())
			image["uri"] = "data:image/png;base64," + Marshalls.raw_to_base64(bytes)

	var state := GLTFState.new()
	var document := GLTFDocument.new()

	if document.append_from_buffer(Library.write_glb(glb["json"], glb["bin"]), "", state) != OK:
		return

	var meshes: Array = state.get_meshes()

	if meshes.is_empty():
		return

	var mesh: Mesh = (meshes[0] as GLTFMesh).mesh.get_mesh()

	if mesh == null:
		return

	EditorInterface.get_resource_previewer().queue_edited_resource_preview(mesh, self, "handle_preview", model)


func handle_preview(_path: String, preview: Texture2D, _thumbnail: Texture2D, model: Variant) -> void:
	if preview == null:
		return

	previews[model] = preview

	for index in range(list.item_count):
		var asset: Dictionary = asset_at(index)

		if not asset.is_empty() and asset["model"] == model:
			list.set_item_icon(index, preview)
