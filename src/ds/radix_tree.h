// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <string_view>

extern "C"
{
#include <lighttpd/radix.h>
}

// Macro to convert a string_view-like to (const void *key, guint32 bits)
// args, for passing to lighttpd radix lib functions
#define TO_LI_KEY(SV) \
  SV.data(), \
    SV.size() * sizeof(std::remove_cvref_t<decltype(SV)>::value_type) * 8

namespace ds
{
  class RadixTree
  {
  private:
    liRadixTree* tree;

  public:
    RadixTree() : tree(li_radixtree_new()) {}
    ~RadixTree()
    {
      li_radixtree_free(tree, nullptr, nullptr);
    }

    using Value = const void*;
    using Key = std::string_view;

    // Inserts a key and value into the tree. Returns pointer to previous
    // element after overwriting, if already present, or nullptr if this is a
    // new key.
    Value insert(const Key& key, Value value)
    {
      return li_radixtree_insert(
        tree, TO_LI_KEY(key), const_cast<void*>(value));
    }

    // Removes a key and associated value from the tree. Returns pointer to
    // previous element that was removed, or nullptr if no such key existed.
    Value remove(const Key& key)
    {
      return li_radixtree_remove(tree, TO_LI_KEY(key));
    }

    // Returns a value previously inserted into the tree, whose key is the
    // longest possible prefix of the given path. If no key in the tree is a
    // prefix of the one given, returns nullptr.
    Value prefix_lookup(const Key& key)
    {
      return li_radixtree_lookup(tree, TO_LI_KEY(key));
    }

    // Returns value inserted at key exactly matching the key given. If there is
    // no exact match, returns nullptr.
    Value exact_lookup(const Key& key)
    {
      return li_radixtree_lookup_exact(tree, TO_LI_KEY(key));
    }

    // TODO: foreach?
  };
}

#undef TO_LI_KEY
