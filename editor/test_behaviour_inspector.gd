extends SceneTree
## test_behaviours.py's Godot half: the Inspector's Behaviour section (behaviour_inspector.gd) on a
## moby of a made-up level scene; writes the metadata it set, step by step, to the file after --.
## Runs in the editor (--editor), since an inspector plugin can be made only there.

const Inspector = preload("res://addons/openrac_library/behaviour_inspector.gd")

var done := false


func _process(_delta: float) -> bool:
	if done:
		return true

	done = true
	var game := Node3D.new()
	game.name = "Game"
	var mobys := Node3D.new()
	mobys.name = "Mobys"
	game.add_child(mobys)
	var moby := Node3D.new()
	moby.name = "Vendor"
	mobys.add_child(moby)
	var tie := Node3D.new()
	game.add_child(tie)
	root.add_child(game)

	var inspector = Inspector.new()
	var steps: Array = [inspector._can_handle(moby), inspector._can_handle(tie)]
	inspector.set_preset(moby, "follow")
	steps.append(Inspector.behaviour_meta(moby))
	inspector.set_number(moby, "speed", 7.5)
	inspector.set_number(moby, "sequence", 3.0)
	inspector.set_preset(moby, "orbit")
	steps.append(Inspector.behaviour_meta(moby))
	var section: Control = inspector.section(moby)
	steps.append(section.get_child_count())
	section.free()
	inspector.set_preset(moby, "")
	steps.append(Inspector.behaviour_meta(moby))
	game.free()

	var file := FileAccess.open(OS.get_cmdline_user_args()[0], FileAccess.WRITE)
	file.store_string(JSON.stringify(steps))
	file.close()
	quit()
	return true
