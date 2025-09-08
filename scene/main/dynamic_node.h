/**************************************************************************/
/*  node.h                                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "core/typedefs.h"
#include "core/variant/dictionary.h"
#include "node.h"
#include "scene/main/scene_tree.h"

class DynamicNode : public Node {
	GDCLASS(DynamicNode, Node);

	mutable StringName target;

protected:
	static void _bind_methods();

public:

	enum TreeModifyType {
		UNINITIALIZE,
		ENTERING,
		MOVING,
		EXITING,
		UPDATING,
	};

	int get_count() const;
	void set_count(int p_count);

	void queue_update();

	DynamicNode();
	~DynamicNode();

private:
	int _data_id = -1;
	int test_key = 0;
	int _units = 1;
	int _prev_units = 0;
	int _prev_tops = 0;
	bool _tpl_node_dirty = false;

	int _unit_mutating_type = UNINITIALIZE;
	int _unit_mutate_index = 0;

	struct ModifyData {
		int inside_target_index = -1;
		int inside_source_index = -1;
		bool final_inside = false;
		bool source_inside = false;
		bool target_inside = false;
		Vector<int> target_ancestors;
		Vector<int> source_ancestors;
		Node *target = nullptr;
	};

	struct Operation {
		Node *node;
		LocalVector<Node *> removing_nodes;
		LocalVector<int> path;
		int top_index;
		int target_index;
		String property = "";
		TreeModifyType type = TreeModifyType::UNINITIALIZE;
	};

	Node *template_root = nullptr;

	void mark_dirty_node(Node *node, TreeModifyType p_modify_type, int p_index, int p_instance, const Vector<int> &p_ancestors);
	void collect_tree_operations(Node *node, TreeModifyType p_modify_type, int p_index, const LocalVector<int> &p_ancestors, const String &p_property = "");
	void perform_tree_operations(int p_unit_pos, HashMap<Node *, Node *> &opr_node_tpl_remap);
	void perform_tree_operations_for_template_node();

	Vector<Node *>& access_top_node(Node *p_tpl_node);
	void remove_top_node(Node *p_tpl_node, int p_index);
	void patch_top_node_cache(int p_unit_pos, Node *p_node, TreeModifyType p_type);

	_FORCE_INLINE_ void add_to_top_node_pool(Node *p_tpl_top_node, Node *top_node) {
		if (!_top_node_pool_map.has(p_tpl_top_node)) {
			Vector<Node *> pool;
			_top_node_pool_map[top_node] = pool;
		}
		_top_node_pool_map[p_tpl_top_node].push_back(top_node);
	};
	_FORCE_INLINE_ void remove_from_top_node_pool(Node *p_tpl_top_node, int index) {
		if (p_tpl_top_node != nullptr && _top_node_pool_map.has(p_tpl_top_node)) {
			Vector<Node *> &pool = _top_node_pool_map[p_tpl_top_node];
			pool.remove_at(index);
			if (pool.size() == 0) {
				_top_node_pool_map.erase(p_tpl_top_node);
			}
		}
	};
	_FORCE_INLINE_ void remove_from_top_node_pool(Node *p_tpl_top_node, Node *top_node) {
		if (p_tpl_top_node != nullptr && _top_node_pool_map.has(p_tpl_top_node)) {
			Vector<Node *> &pool = _top_node_pool_map[p_tpl_top_node];
			pool.erase(top_node);
			if (pool.size() == 0) {
				_top_node_pool_map.erase(p_tpl_top_node);
			}
		}
	};
	// _FORCE_INLINE_ int get_stable_top_index_form_collected_operations(int p_top_index) {
	// 	if (p_top_index == -1) { return p_top_index; }

	// 	int compensate_index = 0;
	// 	for(Operation opr: _operations) {
	// 		if (opr.target_path.size() != 2) { continue; }

	// 		if (opr.type == SceneTree::Enter && opr.target_path[1] != -1 && p_top_index >= opr.target_path[1]) {
	// 			compensate_index++;
	// 		} else if (opr.type == SceneTree::ExitBefore && opr.source_path[1] != -1 && p_top_index >= opr.source_path[1]) {
	// 			compensate_index--;
	// 		}
	// 	}
	// 	return p_top_index + compensate_index;
	// };
	// _FORCE_INLINE_ int get_stable_unit_size_form_collected_operations(int end_index = -1) {
	// 	int compensated_unit_size = get_shadow_node()->get_child_count();

	// 	int size = _operations.size();
	// 	if (end_index < 0) { end_index = size - 1; }
	// 	end_index = MIN(end_index, size - 1);

	// 	for(int i = 0; i <= end_index; i++) {
	// 		Operation const &opr =  _operations[i];
	// 		if (opr.target_path.size() != 2) { continue; }

	// 		if (opr.type == SceneTree::Enter) {
	// 			compensated_unit_size++;
	// 		} else if (opr.type == SceneTree::ExitBefore) {
	// 			compensated_unit_size--;
	// 		}
	// 	}
	// 	return compensated_unit_size;
	// };

	void on_distribute(Vector<Operation> p_operations);

	HashMap<Node *, ModifyData> _modify_data_map;
	// HashMap<Node *, ModifyData> _removed_nodes;
	LocalVector<Operation> _operations;
	int _mutating_unit = -1;
	HashMap<Node *, int> _entered_nodes;
	HashMap<Node *, int> _exited_nodes;
	HashMap<Node *, int> _moved_nodes;
	HashMap<Node *, Node *> _tpl_remap;
	HashMap<Node *, Operation> _node_operaiton_map;
	HashSet<Node *> _top_node_dirty;
	HashSet<Node *> _ignore_tree_mutated;
	HashSet<Node *> _ignore_perform_tree_operations;
	HashSet<Node *> _mutate_top_node_from_operation;
	HashSet<int> mutating_top_indcies;
	HashMap<Node *, int> _top_node_unit_index_map;

	bool _skip_first = false;

	HashMap<Node *, Vector<Node *>> _top_node_pool_map;
	Vector<Node *> _tpl_top_nodes;
	Vector<Node *> _exiting_tpl_top_nodes;
	HashMap<Node *, List<Node *>> _dirty_descendants_map;

	Node *_removed_tpl_node = nullptr;

	void _tree_entered();
	void _tree_exiting();
	void _queue_update_callback();
	void _child_order_changed();
	void _inspector_prop_edited(const String &p_property);

	void _clear_replicas();

	static HashMap<SceneTree*, int> _used_size;
	static HashMap<SceneTree*, List<int>> _unused_id_map;

	static void set_shadow_node_recursive(Node *p_node, Node* p_template_node);
	static void inside_shadow_tree_recursive(Node *p_node, bool p_is_inside);
	static int assign(SceneTree *scene_tree) {
		List<int> unsed_list = _unused_id_map[scene_tree];

		if (unsed_list.size() > 0) {
			int id = unsed_list.front()->get();
			unsed_list.pop_front();
			return id;
		}

		if (!_used_size.has(scene_tree)) { _used_size[scene_tree] = 0; }
		return _used_size[scene_tree]++;
	};
	static void reassign(SceneTree *scene_tree, int id) {
		_unused_id_map[scene_tree].push_back(id);
	};

public:
	void _node_tree_modify(Node *p_node, TreeModifyType p_modify_type, int p_index, Node *p_parent);

	Node* get_template_root() const {
		return template_root;
	}
};

VARIANT_ENUM_CAST(DynamicNode::TreeModifyType);
