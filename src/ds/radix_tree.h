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

    using Target = const void*;
    using Path = std::string_view;

    // Returns pointer to previous element, if already present, after
    // overwriting
    Target insert(const Path& p, Target t)
    {
      return li_radixtree_insert(tree, TO_LI_KEY(p), const_cast<void*>(t));
    }

    Target remove(const Path& p)
    {
      return li_radixtree_remove(tree, TO_LI_KEY(p));
    }

    Target lookup(const Path& p)
    {
      return li_radixtree_lookup(tree, TO_LI_KEY(p));
    }

    Target lookup_exact(const Path& p)
    {
      return li_radixtree_lookup_exact(tree, TO_LI_KEY(p));
    }
  };
}

#undef TO_LI_KEY
