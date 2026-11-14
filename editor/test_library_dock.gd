extends SceneTree
## test_library_dock.py's Godot half: runs addons/openrac_library/library.gd in the project it
## made and writes what it found to the file named after --.

const Library = preload("res://addons/openrac_library/library.gd")


func _initialize() -> void:
	var out: String = OS.get_cmdline_user_args()[0]
	var project: String = ProjectSettings.globalize_path("res://")
	var place: Dictionary = Library.place_of(project)
	var here: Dictionary = Library.here_of("res://levels/level_05/level_05.tscn")
	var folders: Dictionary = Library.game_folders(project)
	var levels: Array = []
	var status: Dictionary = {}
	var names: Dictionary = {}
	var full_asset: Dictionary = {}
	var look_asset: Dictionary = {}

	for game in folders:
		for folder in folders[game]:
			for level in Library.levels_in(game, folder):
				levels.append("%s %s %d %s" % [level["game"], level["disc"], level["level"], level["name"]])

				for asset in Library.assets_of(level):
					var key: String = "%s/%s/%s/%d" % [asset["game"], asset["disc"], asset["kind"], asset["class"]]
					status[key] = Library.status(asset, here)
					names[key] = [asset["name"], asset["scale"]]

					if key == "rac1/pal/mobys/10":
						full_asset = asset
					elif key == "rac1/pal/mobys/75":
						look_asset = asset

	var errors: Array = []
	var result: Dictionary = {"place": {"game": place["game"], "disc": place["disc"]}, "levels": levels,
		"status": status, "names": names, "full_path": Library.prepare(full_asset, here, errors),
		"look_path": Library.prepare(look_asset, here, errors), "errors": errors}
	var file := FileAccess.open(out, FileAccess.WRITE)
	file.store_string(JSON.stringify(result))
	file.close()
	quit()
