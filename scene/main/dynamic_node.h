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

	int _unit_mutating_type = UNINITIALIZE;
	int _unit_mutate_index = 0;

	struct Operation {
		Node *node;
		LocalVector<int> path;
		int top_index;
		int target_index;
		String property = "";
		TreeModifyType type = TreeModifyType::UNINITIALIZE;
	};

	void collect_tree_operations(Node *node, TreeModifyType p_modify_type, int p_index, const LocalVector<int> &p_ancestors, const String &p_property = "");
	void perform_tree_operations(int p_unit_pos, HashMap<Node *, Node *> &opr_node_tpl_remap);
	void perform_tree_operations_for_template_node();

	Vector<Node *>& access_top_node(Node *p_tpl_node);

	void on_distribute(Vector<Operation> p_operations);

	int _mutating_unit = -1;
	LocalVector<Operation> _operations;
	HashMap<Node *, int> _entered_nodes;
	HashMap<Node *, int> _exited_nodes;
	HashMap<Node *, int> _moved_nodes;
	HashMap<Node *, Node *> _tpl_remap;
	HashMap<Node *, Operation> _node_operaiton_map;
	HashSet<Node *> _ignore_tree_mutated;

	HashMap<Node *, Vector<Node *>> _top_node_pool_map;

	void _tree_entered();
	void _tree_exiting();
	void _queue_update_callback();
	void _child_order_changed();
	void _inspector_prop_edited(const String &p_property);

public:
	void request_mutate_tree(Node *p_node, TreeModifyType p_modify_type, int p_index, Node *p_parent);

	int get_unit_index(int p_index) {
		return p_index / _prev_tops;
	};
};

VARIANT_ENUM_CAST(DynamicNode::TreeModifyType);
