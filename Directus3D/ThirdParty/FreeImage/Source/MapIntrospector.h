// ==========================================================
// STL MapIntrospector class
//
// Design and implementation by
// - Carsten Klein (cklein05@users.sourceforge.net)
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

#ifndef MAPINTROSPECTOR_H_
#define MAPINTROSPECTOR_H_

// we need at least one C++ header included, 
// that defines the C++ Standard Library's version macro, 
// that is used below to identify the library.
#include <cstdlib>

/**
Class MapIntrospector - STL std::map Introspector

The MapIntrospector is a helper class used to calculate or estimate part
of the internal memory footprint of a std::map, that is the memory used
by N entries, where N is provided as an argument. This class is used by
function FreeImage_GetMemorySize, which aims to get the total memory
usage for a FreeImage bitmap.

The type argument _Maptype must take the type of the std::map to be
introspected.

This class accounts for 'internal' memory per entry only, that is, the
size returned does neither include the actual size of the std::map class
itself, nor does it include the size of referenced keys and values (also
the actual bytes required for std::string type keys or values are not
counted). For example, the total memory usage should be something like:

typedef std::map<std::string, double> DBLMAP
DBLMAP MyMap;

int total_size = sizeof(DBLMAP) + MapIntrospector<DBLMAP>::GetNodesMemorySize(MyMap.size())
for (DBLMAP::iterator i = MyMap->begin(); i != MyMap->end(); i++) {
 std::string key = i->first;
 total_size += key.capacity();
}

So, basically, this class' task is to get the (constant) number of bytes
per entry, which is multiplied by N (parameter node_count) and returned
in method GetNodesMemorySize. Since this heavily depends on the actually
used C++ Standard Library, this class must be implemented specifically
for each C++ Standard Library.

At current, there is an implementation available for these C++ Standard
Libraries:

- Microsoft C++ Standard Library
- GNU Standard C++ Library v3, libstdc++-v3
- LLVM "libc++" C++ Standard Library (untested)
- Unknown C++ Standard Library

Volunteers for testing as well as for providing support for other/new
libraries are welcome.

The 'Unknown C++ Standard Library' case is used if no other known C++
Standard Library was detected. It uses a typical _Node structure to
declare an estimated memory consumption for a node.
*/

#if defined(_CPPLIB_VER)	// Microsoft C++ Standard Library
/**
 The Microsoft C++ Standard Library uses the protected structure _Node
 of class std::_Tree_nod to represent a node. This class is used by
 std::_Tree, the base class of std::map. So, since std::map is derived
 from std::_Tree (and _Node is protected), we can get access to this
 structure by deriving from std::map.

 Additionally, the Microsoft C++ Standard Library uses a separately
 allocated end node for its balanced red-black tree so, actually, there
 are size() + 1 nodes present in memory.

 With all that in place, the total memory for all nodes in std::map
 is simply (node_count + 1) * sizeof(_Node).
*/
template<class _Maptype>
class MapIntrospector: private _Maptype {
public:
	static size_t GetNodesMemorySize(size_t node_count) {
		return (node_count + 1) * sizeof(_Node);
	}
};

#elif defined(__GLIBCXX__)	// GNU Standard C++ Library v3, libstdc++-v3
/**
 The GNU Standard C++ Library v3 uses structure std::_Rb_tree_node<_Val>,
 which is publicly declared in the standard namespace. Its value type
 _Val is actually the map's value_type std::map::value_type.

 So, the total memory for all nodes in std::map is simply
 node_count * sizeof(std::_Rb_tree_node<_Val>), _Val being the map's
 value_type.
*/
template<class _Maptype>
class MapIntrospector {
private:
	typedef typename _Maptype::value_type _Val;

public:
	static size_t GetNodesMemorySize(size_t node_count) {
		return node_count * sizeof(std::_Rb_tree_node<_Val>);
	}
};

#elif defined(_LIBCPP_VERSION)	// "libc++" C++ Standard Library (LLVM)
/*
 The "libc++" C++ Standard Library uses structure
 std::__tree_node<_Val, void*> for regular nodes and one instance of
 structure std::__tree_end_node<void*> for end nodes, which both are
 publicly declared in the standard namespace. Its value type _Val is
 actually the map's value_type std::map::value_type.

 So, the total memory for all nodes in std::map is simply
 node_count * sizeof(std::__tree_node<_Val, void*>)
 + sizeof(std::__tree_end_node<void*>).
 
 REMARK: this implementation is not yet tested!
*/
template<class _Maptype>
class MapIntrospector {
private:
	typedef typename _Maptype::value_type _Val;

public:
	static size_t GetNodesMemorySize(size_t node_count) {
		return node_count * sizeof(std::__tree_node<_Val, void*>) + sizeof(std::__tree_end_node<void*>);
	}
};

//#elif defined(_ADD_YOUR_CPP_STD_LIBRARY_HERE_)

#else							// Unknown C++ Standard Library
/**
 If we do not know the actual C++ Standard Library and so, have no
 access to any internal types, we can just make some assumptions about
 the implementation and memory usage.

 However, all implementations will probably be based on a balanced
 red-black tree, will also store the map's value_type in each node and
 need some extra information like the node's color. For a binary tree,
 at least two pointers, one for left and one for right are required.
 Since it is handy, many implementations also have a parent pointer.

 We let the compiler calculate the size of the above mentioned items by
 using a fake structure. By using a real structure (in contrast to just
 adding numbers/bytes) we'll get correct pointer sizes as well as any
 padding applied for free.
*/
template<class _Maptype>
class MapIntrospector {
private:
	/* Define some handy typedefs to build up the structure. */

	/**
	 Each node will likely store the value_type of the mapping,
	 that is a std::pair<_Key, _Value>.
	*/
	typedef typename _Maptype::value_type _Val;

	/**
	 We will need some pointers, since std::map is likely implemented
	 as a balanced red-black tree.
	*/
	typedef void*                         _Ptr;

	/**
	 Space for some extra information (like the node's color).
	 An int should be sufficient.
	*/
	typedef int                           _Ext;

	/* The memory required for each node will likely look like this
	 structure. We will just multiply sizeof(_Node) by the number
	 of nodes to get the total memory of all nodes. By using the
	 size of the structure, we will also take care of the compiler's
	 default padding.
	*/
	typedef struct {
		_Ptr _parent_node;
		_Ptr _left_node;
		_Ptr _right_node;
		_Val _value;
		_Ext _extra_info;
	} _Node;

public:
	static size_t GetNodesMemorySize(size_t node_count) {
		return node_count * sizeof(_Node);
	}
};

#endif // Standard C++ Library

#endif // MAPINTROSPECTOR_H_
