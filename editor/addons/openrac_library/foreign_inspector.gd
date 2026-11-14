@tool
extends EditorInspectorPlugin
## The Imported class section in the Inspector of a moby that came with its own logic from another
## level (rc1_foreign_level metadata, the Assets dock's "Add with its own logic"): where its logic
## runs from, and what to expect of it. editor/foreign_classes.py packs it.

const BehaviourInspector = preload("behaviour_inspector.gd")

## A level's name by its number, or "" (the dock knows the levels read).
var level_name: Callable = func(_level: int) -> String: return ""


func _can_handle(object: Object) -> bool:
	var node := object as Node
	return BehaviourInspector.is_moby(node) and node.has_meta("rc1_foreign_level")


func _parse_begin(object: Object) -> void:
	add_custom_control(section(object as Node3D))


## The section for NODE.
func section(node: Node3D) -> Control:
	var panel := PanelContainer.new()
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.55, 0.78, 1.0, 0.12)
	style.border_color = Color(0.55, 0.78, 1.0, 0.6)
	style.border_width_left = 3
	style.content_margin_left = 8
	style.content_margin_right = 6
	style.content_margin_top = 4
	style.content_margin_bottom = 4
	panel.add_theme_stylebox_override("panel", style)

	var text := RichTextLabel.new()
	text.bbcode_enabled = true
	text.fit_content = true
	text.scroll_active = false
	text.custom_minimum_size = Vector2(160, 0)
	var level: int = int(node.get_meta("rc1_foreign_level", -1))
	var name: String = String(level_name.call(level))
	text.text = ("[b]Imported class[/b]\nIts own logic runs from level %02d%s, as it does there. "
		+ "[color=#9aa4b2]Pack, then play. Its level's paths, zones and cameras come with it, "
		+ "moved to where it stands, and the mobys it needs there with them; its sounds and texts "
		+ "are this level's. Each costs about half a millisecond a frame.[/color]") % [level, "" if name.is_empty() else " (%s)" % name]
	panel.add_child(text)
	return panel
