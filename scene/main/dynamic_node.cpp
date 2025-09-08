/**************************************************************************/
/*  node.cpp                                                              */
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

#include "dynamic_node.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_method_pointer.h"
#include "core/object/object.h"
#include "core/os/memory.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"
#include "core/typedefs.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"
#include "scene/gui/container.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "editor/docks/inspector_dock.h"

HashMap<SceneTree*, int> DynamicNode::_used_size;
HashMap<SceneTree*, List<int>> DynamicNode::_unused_id_map;

static LocalVector<int> DontWantAncestors;
static Node* get_ancestor(Node *node, int distance, LocalVector<int> &ancestors = DontWantAncestors) {
	if (distance < 0) { return nullptr; }

	Node *visited_node = node;
	int pos = 0;
	while (pos++ < distance) {
		visited_node = visited_node->get_parent();
		if (&ancestors != &DontWantAncestors) {
			ancestors.push_back(visited_node->get_index());
		}
	}
	if(&ancestors != &DontWantAncestors) { ancestors.reverse(); }
	return visited_node;
}

static Node* get_closest_daynamic_ancestor(Node *node, int distance) {
	Node *p = node;
	int i = 0;
	while (i++ < distance) {
		p = p->get_parent();
		if (p->is_dynamic_type(Node::DYNAMIC_TOP_NODE)) {
			return p;
		}
	}
	return nullptr;
}

// TODO: 考虑支持 shadow node
static Node* get_descendant(Node *node, const Vector<int> &descendants, int skip_front = 0, int skip_back = 0) {
	skip_front = MAX(0, skip_front);
	skip_back = MAX(0, skip_back);

	if (descendants.size() - skip_front - skip_back < 0) { return nullptr; }

	Node *visited_node = node;
	for (int i = skip_front; i < descendants.size() - skip_back; i++) {
		visited_node = visited_node->get_child(descendants[i] < 0 ? visited_node->get_child_count() + descendants[i] : descendants[i]);
	}
	return visited_node;
}

int DynamicNode::get_count() const {
	return _units;
}

void DynamicNode::set_count(int p_count) {
	if(_units == p_count) { return; }
	_units = p_count;
	queue_update();
}

static void set_owner_recursive(Node *p_node, Node* p_owner) {
	p_node->set_owner(p_owner);
	for (int i = 0; i < p_node->get_child_count(); i++) {
		set_owner_recursive(p_node->get_child(i), p_owner);
	}
}

static void set_descendants_dynamic_root(Node *p_node, Node* p_dynamic_root) {
	p_node->set_dynamic_root(p_dynamic_root);
	for (int i = 0; i < p_node->get_child_count(); i++) {
		set_descendants_dynamic_root(p_node->get_child(i), p_dynamic_root);
	}
}

void DynamicNode::set_shadow_node_recursive(Node *p_node, Node* p_shadow_node) {
	// p_node->set_shadow_node(p_shadow_node);
	// for (int i = 0; i < p_node->get_child_count(); i++) {
	// 	set_shadow_node_recursive(p_node->get_child(i), p_shadow_node->get_child(i));
	// }
}
void DynamicNode::inside_shadow_tree_recursive(Node *p_node, bool p_is_inside) {
	// p_node->is_inside_shadow_tree = p_is_inside;
	// for (int i = 0; i < p_node->get_child_count(); i++) {
	// 	inside_shadow_tree_recursive(p_node->get_child(i), p_is_inside);
	// }
}

#define INDEX_HANDLE													\
	unsigned int index = p_index < 0 ? list.size() + p_index : p_index;	\
	CRASH_BAD_INDEX(index, list.size());

template<typename T>
_FORCE_INLINE_ static void vector_set(LocalVector<T> &list, int p_index, T value) {
	INDEX_HANDLE
	list[index] = value;
}
template<typename T>
_FORCE_INLINE_ static void vector_set(Vector<T> &list, int p_index, T value) {
	INDEX_HANDLE
	list.set(index, value);
}

template<typename T>
_FORCE_INLINE_ static T& vector_get(LocalVector<T> list, int p_index) {
	INDEX_HANDLE
	return list[index];
}
template<typename T>
_FORCE_INLINE_ static T& vector_get(Vector<T> list, int p_index) {
	INDEX_HANDLE
	return list[index];
}

template<typename T>
_FORCE_INLINE_ static LocalVector<T> vector_append(const LocalVector<T> list, T value) {
	LocalVector<T> new_list = list;
	new_list.push_back(value);
	return new_list;
}
template<typename T>
_FORCE_INLINE_ static Vector<T> vector_append(const Vector<T> list, T value) {
	Vector<T> new_list = list;
	new_list.push_back(value);
	return new_list;
}

static int flags_replica = Node::DUPLICATE_SIGNALS | Node::DUPLICATE_GROUPS | Node::DUPLICATE_SCRIPTS | Node::DUPLICATE_FROM_EDITOR;

void DynamicNode::on_distribute(Vector<Operation> p_operations) {
	_operations = p_operations;
	queue_update();
}

void DynamicNode::patch_top_node_cache(int p_unit_pos, Node *p_node, TreeModifyType p_type) {
	Node *tpl_node = _tpl_remap[p_node];

	switch (p_type) {
		case ENTERING: {
			Node *tpl_node = _tpl_remap[p_node];
			Vector<Node*> &pool = access_top_node(tpl_node);
			if (pool.size() <= p_unit_pos) {
				pool.resize(p_unit_pos + 1);
				pool.set(p_unit_pos, p_node);
			} else if (!pool[p_unit_pos]) {
				pool.set(p_unit_pos, p_node);
			}
		} break;

		case EXITING: {
			remove_top_node(tpl_node, p_unit_pos);
		} break;

		default: break;
	}
}

void DynamicNode::perform_tree_operations_for_template_node() {
	Node *removed_node = nullptr;

	for (Operation &opr : _operations) {
		LocalVector<int> path = opr.path;
		Node *terminal_node = get_descendant(template_root, path, 1, 0);

		switch (opr.type) {
			case ENTERING: {
				Node *tpl_node;

				if (removed_node) {
					tpl_node = removed_node;
				} else {
					tpl_node = opr.node->duplicate(flags_replica);
				}

				_ignore_tree_mutated.insert(tpl_node);
				tpl_node->append_to(terminal_node);
				set_owner_recursive(tpl_node, template_root);

				if (opr.path.size() > 1) { break; }

				// 当sub提升为top时，在处理template时统一生成所有的node并和tpl联系起来，遍历replica时不生成而是直接使用
				access_top_node(tpl_node).resize(_units);
				Operation *exited_opr = _exited_nodes.size() > 0 ? &_operations[0] : nullptr;
				LocalVector<int> exited_opr_path = exited_opr->path;
				for (int i = 0; i < _units; i++) {
					Node *replica_node = nullptr;

					if (i == _mutating_unit) {
						replica_node = opr.node;
						if (_unit_mutating_type == ENTERING) {
							exited_opr_path[1] += 1;
						} else if (_unit_mutating_type == EXITING) {
							exited_opr_path[1] -= 1;
						}
					} else if(_exited_nodes.has(opr.node)) {
						replica_node = get_descendant(this, exited_opr_path, 1, 0);
					} else {
						replica_node = opr.node->duplicate(flags_replica);
					}

					exited_opr_path[1] += _prev_tops;
					Vector<Node *> &pool = access_top_node(tpl_node);
					pool.set(i, replica_node);
					_tpl_remap[replica_node] = tpl_node;
				}
			} break;

			case EXITING: {
				if (!terminal_node) { return; }

				_ignore_tree_mutated.insert(terminal_node);
				terminal_node->remove();

				if (_entered_nodes.has(opr.node)) {
					removed_node = terminal_node;
				} else {
					terminal_node->queue_free();
				}

				if (opr.path.size() == 2) {
					_top_node_pool_map.erase(_tpl_remap[terminal_node]);
				}
			} break;

			case MOVING: {
				if (!terminal_node) { return; }

				_ignore_tree_mutated.insert(terminal_node);
				terminal_node->move_to(opr.target_index);
			} break;
			case UPDATING: {
				if (opr.property != "") {
					terminal_node->set(opr.property, opr.node->get(opr.property));
				}
			} break;
			default: break;
		}
	}
}

void DynamicNode::perform_tree_operations(int p_unit_pos, HashMap<Node *, Node *> &opr_top_tpl_remap) {
	HashMap<Node *, Node *> removed_nodes;
	LocalVector<Node *> target_parents;
	LocalVector<Node *> source_nodes;

	int unit_size = template_root->get_child_count();
	Node *owner = get_owner();
	int top_offset = unit_size * p_unit_pos;

	for (unsigned int i = 0; i < _operations.size(); i++) {
		Operation const &opr = _operations[i];

		Node *terminal_node = nullptr;
		LocalVector<int> path = opr.path;

		if (path.size() > 1) {
			int top_index = vector_get(path, 1);

			if (top_index != -1) {
				vector_set(path, 1, top_offset + top_index);
			}
		} // else: ENTERING to top node, nothing to do

		terminal_node = get_descendant(this, path, 1, 0);

		switch (opr.type) {
			case ENTERING: {
				if (!terminal_node) { return; }

				Node *node;

				if (opr.path.size() == 1) {
					Node *tpl = _tpl_remap[opr.node];
					Vector<Node *> &pool = access_top_node(tpl);
					node = pool[p_unit_pos];
				} else if(removed_nodes.has(opr.node)) {
					node = removed_nodes[opr.node];
				} else {
					node = opr.node->duplicate(flags_replica);
				}

				_ignore_tree_mutated.insert(node);
				node->append_to(terminal_node);
				set_owner_recursive(node, owner);
			} break;

			case EXITING: {
				if (!terminal_node) { return; }

				_ignore_tree_mutated.insert(terminal_node);
				terminal_node->remove();

				if (_entered_nodes.has(opr.node)) {
					removed_nodes[opr.node] = terminal_node;
				} else {
					_tpl_remap.erase(terminal_node);
					terminal_node->queue_free();
				}
			} break;
			case MOVING: {
				if (!terminal_node) { return; }

				_ignore_tree_mutated.insert(terminal_node);
				int offset = opr.path.size() == 2 ? top_offset : 0;
				terminal_node->move_to(offset + opr.target_index);
			} break;
			case UPDATING: {
				if (opr.property != "") {
					terminal_node->set(opr.property, opr.node->get(opr.property));
				}
			} break;
			default: break;
		}
	}
}

void DynamicNode::collect_tree_operations(Node* p_node, TreeModifyType p_modify_type, int p_index, const LocalVector<int> &p_ancestors, const String &p_property) {
	LocalVector<int> path = p_ancestors;
	int source_index = p_node->get_index();
	int target_index = p_index;

	if (p_modify_type == MOVING && _entered_nodes.has(p_node)) {
		source_index = -1;
	}

	if (p_modify_type != ENTERING) {
		path.push_back(source_index);
	}

	int top_index = -1;
	if (path.size() > 1) {
		int current_unit_size = _prev_tops;

		top_index = vector_get(path, 1);

		bool x = p_ancestors.size() == 1 && p_modify_type == MOVING;
		int terminal_top_index = x ? target_index : top_index;
		int unit_index = terminal_top_index / current_unit_size;

		// if (top_index != -1) {
		if ( _unit_mutating_type == ENTERING && terminal_top_index >= _unit_mutate_index) {
			// final_top_index++;
		} else if (_unit_mutating_type == EXITING && terminal_top_index < _unit_mutate_index) {
			// terminal_top_index--;
		}

		terminal_top_index %= current_unit_size;
		if (terminal_top_index == 0 && _exited_nodes.has(p_node)) {
			Operation &exited_opr = _operations[_exited_nodes[p_node]];
			if (unit_index == exited_opr.top_index / current_unit_size) {
				terminal_top_index = 0;
			} else {
				terminal_top_index = current_unit_size;
			}
		}

		if (x) {
			target_index = terminal_top_index;
			if (!_entered_nodes.has(p_node)) {
				path[1] %= current_unit_size;
			}
		} else {
			vector_set(path, 1, terminal_top_index);
		}

	}

	if (p_ancestors.size() == 1) {
		int current_unit_size = template_root->get_child_count();
		if (p_modify_type == ENTERING) {
			_unit_mutating_type = ENTERING;
			_unit_mutate_index = current_unit_size;
		} else if (p_modify_type == EXITING) {
			_unit_mutating_type = EXITING;
			_unit_mutate_index = source_index % current_unit_size;
		} else if (p_modify_type == MOVING && _entered_nodes.has(p_node)) {
			_unit_mutate_index = target_index % current_unit_size;
			if (_unit_mutate_index == 0 && _exited_nodes.has(p_node)) {
				_unit_mutate_index = current_unit_size;
			}
		}
	};

	Operation operation;
	operation.node = p_node;
	operation.type = p_modify_type;
	operation.property = p_property;
	operation.path = path;
	operation.target_index = target_index;
	operation.top_index = top_index;

	if (p_modify_type == EXITING) {
		LocalVector<Node *> removing_nodes;
		removing_nodes.resize(_units);

		LocalVector<int> start_path = path;
		for (int i = 0; i < _units; i++) {
			removing_nodes[i] = get_descendant(this, start_path, 1, 0);
			start_path[1] += template_root->get_child_count();
		}
		operation.removing_nodes = removing_nodes;
	}

	if (p_modify_type == ENTERING) {
		_entered_nodes[p_node] = _operations.size();
	} else if (p_modify_type == EXITING) {
		_exited_nodes[p_node] = _operations.size();
	} else if (p_modify_type == MOVING) {
		_moved_nodes[p_node] = _operations.size();
	}

	_operations.push_back(operation);

}

// filter modified for descendant
void DynamicNode::_node_tree_modify(Node *p_node, TreeModifyType p_modify_type, int p_index, Node *p_parent) {
	if (_ignore_tree_mutated.has(p_node)) { _ignore_tree_mutated.erase(p_node); return; }

	int distance;
	Node *ancestor;
	LocalVector<int> ancestors;

	if (p_modify_type == ENTERING) {
		distance = p_parent->_get_scene_tree_depth() - _get_scene_tree_depth() + 1;
		ancestor = get_ancestor(p_parent, distance - 1, ancestors);
		ancestors.push_back(p_parent->get_index());
	} else {
		distance = p_node->_get_scene_tree_depth() - _get_scene_tree_depth();
		ancestor = get_ancestor(p_node, distance, ancestors);
	}


	// bool is_source_dynamic_node = get_closest_daynamic_ancestor(p_node, distance) == this;

	if (distance > 0
		// && ancestor == this
		// // && is_source_dynamic_node
		// && (p_node->is_daynamic_type(Node::DYNAMIC_REPLICA))
	) {
		collect_tree_operations(p_node, p_modify_type, p_index, ancestors);
		queue_update();
	}
}

_FORCE_INLINE_ Vector<Node *>& DynamicNode::access_top_node(Node *p_tpl_node) {
	if(!_top_node_pool_map.has(p_tpl_node)){
		Vector<Node *> pool;
		_top_node_pool_map[p_tpl_node] = pool;
	}
	Vector<Node *> &pool = _top_node_pool_map[p_tpl_node];

	return pool;
}

_FORCE_INLINE_ void DynamicNode::remove_top_node(Node *p_tpl_node, int p_index) {
	// TODO: 考虑换成断言？
	if (!_top_node_pool_map.has(p_tpl_node)) {
		return;
	}

	Vector<Node *> &pool = _top_node_pool_map[p_tpl_node];
	if (pool.size() == 1) {
		_top_node_pool_map.erase(p_tpl_node);
	} else {
		pool.remove_at(p_index);
	}
}

void DynamicNode::_queue_update_callback() {
	int flags = DUPLICATE_SIGNALS | DUPLICATE_GROUPS | DUPLICATE_SCRIPTS | DUPLICATE_FROM_EDITOR;

	int max_units = MAX(_units, _prev_units);
	int desire_units = MAX(_units, 0);

	for (unsigned int i = 0; i < _operations.size(); i++) {
		Operation &opr = _operations[i];
		switch (opr.type) {
			case EXITING:
			case UPDATING:
				_mutating_unit = opr.top_index / _prev_tops;
				break;
			case ENTERING:
				if (opr.path.size() > 1) {
					_mutating_unit = opr.top_index / _prev_tops;
				} else {
					_mutating_unit = _units - 1;
				}
				break;
			case MOVING: {
				if (_exited_nodes.has(opr.node) && opr.path.size() == 2) {
					Operation &exited_opr = _operations[_exited_nodes.get(opr.node)];
					int exited_top_index = exited_opr.top_index;
					_mutating_unit = exited_top_index / _prev_tops;
				} else {
					_mutating_unit = opr.top_index / _prev_tops;
				}
			} break;
			case UNINITIALIZE:
				break;
		}
	}

	HashMap<Node *, Node *> opr_top_tpl_remap;
	// perform_tree_operations(-1, opr_top_tpl_remap);
	perform_tree_operations_for_template_node();

	int desire_tpls = template_root->get_child_count(true);

	// patch for extra move_to operation
	for (unsigned int i = 0; i < _operations.size(); i++) {
		Operation &opr = _operations[i];
		bool append_only = _entered_nodes.has(opr.node) && !_exited_nodes.has(opr.node) && !_moved_nodes.has(opr.node);
		if (append_only && opr.path.size() == 2) {
			Operation extra_opr = opr;
			extra_opr.type = MOVING;
			vector_set(extra_opr.path, 1, desire_tpls - 1);
		}
	}

	if (desire_units == _prev_units && desire_tpls == _prev_tops && _operations.size() == 0) { return; }

	for (int unit_pos = 0; unit_pos < max_units; unit_pos++) {
		if (_mutating_unit == unit_pos) {
			// for (Operation &opr : _operations) {
			// 	if ((opr.type == ENTERING && opr.path.size() == 1) || (opr.type != ENTERING && opr.path.size() == 2)) {
			// 		patch_top_node_cache(unit_pos, opr.node, opr.type);
			// 	}
			// }
			continue;
		}

		perform_tree_operations(unit_pos, opr_top_tpl_remap);
		for(int tpl_pos = 0; tpl_pos < template_root->get_child_count(true); tpl_pos++) {
			Node *tpl_top_node = template_root->get_child(tpl_pos, true);

			// TODO: 如何为刚添加的 top_node 增加 top_pool_cache

			Vector<Node *> &top_node_pool = access_top_node(tpl_top_node);

			if (unit_pos < desire_units) {
				if (top_node_pool.size() <= unit_pos) {
					Node *top_node = tpl_top_node->duplicate(flags);
					_tpl_remap[top_node] = tpl_top_node;

					_ignore_tree_mutated.insert(top_node);
					add_child(top_node);
					set_owner_recursive(top_node, get_owner());
					set_descendants_dynamic_root(top_node, this);
					top_node_pool.push_back(top_node);
				}
			} else {
				int remove_index_start = desire_units;
				Node *top_node = top_node_pool[remove_index_start];
				_tpl_remap.erase(top_node);
				remove_top_node(tpl_top_node, remove_index_start);

				_ignore_tree_mutated.insert(top_node);
				top_node->remove();
				top_node->queue_free();
			}
		}
	}

	_mutating_unit = -1;
	_prev_units = _units;
	_prev_tops = desire_tpls;
	_exiting_tpl_top_nodes.clear();
	_modify_data_map.clear();
	_dirty_descendants_map.clear();
	_top_node_dirty.clear();
	_operations.clear();
	_exited_nodes.clear();
	_entered_nodes.clear();
	_mutate_top_node_from_operation.clear();
	_ignore_perform_tree_operations.clear();
	_skip_first = false;
	_top_node_unit_index_map.clear();

	_unit_mutating_type = UNINITIALIZE;
}

// void DynamicNode::sync_for_add_child(Node *p_child) {
// 	Operation opr {SceneTree::Enter, p_child};
// };
// void DynamicNode::sync_for_remove_child(Node *p_child) {

// };
// void DynamicNode::sync_for_move_child(Node *p_child, int p_index) {

// };

void DynamicNode::queue_update(){
	callable_mp(this, &DynamicNode::_queue_update_callback).call_deferred();
}

void DynamicNode::_bind_methods() {
	BIND_ENUM_CONSTANT(UNINITIALIZE);
	BIND_ENUM_CONSTANT(ENTERING);
	BIND_ENUM_CONSTANT(MOVING);
	BIND_ENUM_CONSTANT(EXITING);
	BIND_ENUM_CONSTANT(UPDATING);

	ClassDB::bind_method(D_METHOD("get_count"), &DynamicNode::get_count);
	ClassDB::bind_method(D_METHOD("set_count", "value"), &DynamicNode::set_count);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "count"), "set_count", "get_count");

}

void DynamicNode::_tree_entered() {
	InspectorDock::get_inspector_singleton()->connect("property_edited", callable_mp(this, &DynamicNode::_inspector_prop_edited));
	queue_update();
}

void DynamicNode::_tree_exiting() {
	InspectorDock::get_inspector_singleton()->disconnect("property_edited", callable_mp(this, &DynamicNode::_inspector_prop_edited));
}

void DynamicNode::_clear_replicas() {
	_top_node_pool_map.clear();
	_tpl_top_nodes.clear();
}

void DynamicNode::_inspector_prop_edited(const String &p_property) {
	Object *object = InspectorDock::get_inspector_singleton()->get_edited_object();
	Node *node = Object::cast_to<Node>(object);
	if (!node) { return; }

	int distance = node->_get_scene_tree_depth() - _get_scene_tree_depth();
	LocalVector<int> ancestors;
	get_ancestor(node, distance, ancestors);

	bool is_source_dynamic_node = get_closest_daynamic_ancestor(node, distance) == this;
	if (!is_source_dynamic_node) { return; }

	collect_tree_operations(node,  UPDATING, -1, ancestors, p_property);
	queue_update();
}

DynamicNode::DynamicNode() {
	template_root = memnew(Node);
	template_root->set_inside_template_tree(true);
	template_root->set_dynamic_root(this);
	set_dynamic_root(this);
	set_template_root(template_root);

	connect(SceneStringName(tree_entered), callable_mp(this, &DynamicNode::_tree_entered));
	connect(SceneStringName(tree_exiting), callable_mp(this, &DynamicNode::_tree_exiting));
}

DynamicNode::~DynamicNode() {
	memdelete(template_root);

	disconnect(SceneStringName(tree_entered), callable_mp(this, &DynamicNode::_tree_entered));
	disconnect(SceneStringName(tree_exiting), callable_mp(this, &DynamicNode::_tree_exiting));
}
