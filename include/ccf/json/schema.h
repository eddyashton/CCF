// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/json_schema.h"
#include "ccf/json/serialization.h"

#include <algorithm>
#include <glaze/json/schema.hpp>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <typeindex>

namespace glz
{
  // Schema naming metadata must not hide CCF's explicit nullable ADL leaves.
  template <typename T>
    requires ccf::json::detail::AdlCandidate<std::shared_ptr<T>>
  struct meta<std::shared_ptr<T>>
  {
    static constexpr std::string_view name =
      join_v<chars<"std::shared_ptr<">, name_v<T>, chars<">">>;
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  template <typename T>
    requires ccf::json::detail::AdlCandidate<std::unique_ptr<T>>
  struct meta<std::unique_ptr<T>>
  {
    static constexpr std::string_view name =
      join_v<chars<"std::unique_ptr<">, name_v<T>, chars<">">>;
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };
}

namespace ccf::json::detail
{
  inline constexpr auto schema_opts = [] {
    struct SchemaOptions : ccf::json::Options
    {
      bool write_type_info = false;
    };
    auto options = SchemaOptions{ccf::json::options};
    // These are optional members of glz::schema, not of the described type.
    options.skip_null_members = true;
    return options;
  }();

  inline std::string component_name(std::string_view name)
  {
    std::string result(name);
    for (auto& c : result)
    {
      const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
      if (!valid)
      {
        c = '_';
      }
    }
    return result;
  }

  // Only recurse into schema-valued keywords, never examples/default/const
  // data.
  template <typename Transform>
  Json transform_schema(Json schema, const Transform& transform)
  {
    if (schema.is_boolean())
    {
      return schema;
    }
    if (!schema.is_object())
    {
      throw std::logic_error("Expected a JSON Schema object or boolean");
    }
    for (const auto* key :
         {"$defs", "properties", "patternProperties", "dependentSchemas"})
    {
      if (schema.contains(key))
      {
        for (const auto& [name, child] : schema[key].items())
        {
          child = transform_schema(std::move(child), transform);
        }
      }
    }
    for (const auto* key :
         {"items",
          "additionalProperties",
          "contains",
          "propertyNames",
          "unevaluatedItems",
          "unevaluatedProperties",
          "not",
          "if",
          "then",
          "else",
          "contentSchema"})
    {
      if (schema.contains(key))
      {
        schema[key] = transform_schema(std::move(schema[key]), transform);
      }
    }
    for (const auto* key : {"prefixItems", "allOf", "anyOf", "oneOf"})
    {
      if (schema.contains(key))
      {
        for (auto& child : schema[key])
        {
          child = transform_schema(std::move(child), transform);
        }
      }
    }
    return transform(std::move(schema));
  }

  inline Json prepare_schema(Json schema)
  {
    auto adl_definitions = Json::object();
    if (schema.is_object() && schema.contains("$defs"))
    {
      auto& definitions = schema["$defs"];
      for (auto it = definitions.begin(); it != definitions.end();)
      {
        if (
          it.key().starts_with("ccf.adl.") ||
          it.key().starts_with("ccf.openapi."))
        {
          adl_definitions[it.key()] = std::move(it.value());
          it = definitions.erase(it);
        }
        else
        {
          ++it;
        }
      }
    }
    auto result = transform_schema(std::move(schema), [](Json node) {
      if (
        node.contains("const") && node.contains("title") &&
        node["title"] == node["const"])
      {
        node.erase("title");
      }
      if (node.contains("oneOf") && !node["oneOf"].empty())
      {
        auto values = Json::array();
        bool plain_enum = true;
        for (const auto& branch : node["oneOf"])
        {
          if (
            !branch.is_object() || !branch.contains("const") ||
            branch.size() != (branch.contains("title") ? 2 : 1) ||
            (branch.contains("title") && branch["title"] != branch["const"]) ||
            std::find(values.begin(), values.end(), branch["const"]) !=
              values.end())
          {
            plain_enum = false;
            break;
          }
          values.push_back(branch["const"]);
        }
        if (plain_enum)
        {
          node.erase("oneOf");
          node["enum"] = std::move(values);
        }
      }
      if (
        node.value("type", Json()) == "object" &&
        node.contains("additionalProperties") &&
        node["additionalProperties"].is_boolean())
      {
        // Generated record schemas need no explicit permissive default.
        node.erase("additionalProperties");
      }
      if (
        node.contains("prefixItems") && node.value("items", Json()) == false &&
        node.value("maxItems", Json()) == node["prefixItems"].size() &&
        !node.contains("minItems"))
      {
        // Glaze 9 emits the upper tuple bound but omits the lower bound.
        node["minItems"] = node["prefixItems"].size();
      }
      return node;
    });
    for (const auto& [name, definition] : adl_definitions.items())
    {
      result["$defs"][name] = std::move(definition);
    }
    return result;
  }

  inline Json rebase_schema(
    const Json& schema,
    const std::string& base,
    bool keep_component_references = false)
  {
    std::set<std::string> anchors;
    transform_schema(schema, [&](Json node) {
      if (node.contains("$anchor"))
      {
        anchors.insert(node["$anchor"].get<std::string>());
      }
      return node;
    });
    // Independent custom schemas may use the same local anchor names.
    std::string anchor_prefix = "ccf.";
    constexpr std::string_view hex = "0123456789abcdef";
    for (const unsigned char c : base)
    {
      anchor_prefix += hex[c >> 4];
      anchor_prefix += hex[c & 15];
    }
    anchor_prefix += '.';
    return transform_schema(schema, [&](Json node) {
      if (node.contains("$anchor"))
      {
        node["$anchor"] = anchor_prefix + node["$anchor"].get<std::string>();
      }
      if (node.contains("$ref"))
      {
        const auto reference = node.at("$ref").get<std::string>();
        if (
          keep_component_references &&
          reference.starts_with("#/components/schemas/"))
        {
          return node;
        }
        if (reference == "#" || reference.starts_with("#/"))
        {
          // Fail on dangling local references before publishing a document.
          (void)schema.at(Json::json_pointer(reference.substr(1)));
          node["$ref"] = base + reference.substr(1);
        }
        else if (
          reference.starts_with('#') && anchors.contains(reference.substr(1)))
        {
          node["$ref"] = "#" + anchor_prefix + reference.substr(1);
        }
      }
      return node;
    });
  }

  inline Json local_schema_reference(const Json& root, const std::string& ref)
  {
    if (ref == "#" || ref.starts_with("#/"))
    {
      return root.at(Json::json_pointer(ref.substr(1)));
    }
    std::optional<Json> target;
    transform_schema(root, [&](Json node) {
      if (node.value("$anchor", Json()) == ref.substr(1))
      {
        if (target.has_value())
        {
          throw std::logic_error("Ambiguous schema anchor: " + ref);
        }
        target = node;
      }
      return node;
    });
    if (!target.has_value())
    {
      throw std::logic_error("Unknown schema anchor: " + ref);
    }
    return *target;
  }

  inline Json inline_references(
    Json source, const Json& root, std::set<std::string>& expanding)
  {
    if (source.is_boolean())
    {
      return source;
    }
    source.erase("$defs");
    source.erase("$schema");
    return transform_schema(std::move(source), [&](Json node) {
      node.erase("$anchor");
      if (!node.contains("$ref"))
      {
        return node;
      }
      const auto reference = node.at("$ref").get<std::string>();
      if (!reference.starts_with('#'))
      {
        throw std::logic_error(
          "External references cannot be expanded in a standalone schema");
      }
      if (!expanding.insert(reference).second)
      {
        throw std::logic_error("Recursive schemas require a component schema");
      }
      auto target = inline_references(
        local_schema_reference(root, reference), root, expanding);
      expanding.erase(reference);
      node.erase("$ref");
      return node.empty() ? target :
                            Json{{"allOf", Json::array({target, node})}};
    });
  }

  struct SchemaExpansion
  {
    std::set<std::string>& active;
    std::string name;

    SchemaExpansion(std::set<std::string>& active_, std::string name_) :
      active(active_),
      name(std::move(name_))
    {
      active.insert(name);
    }

    ~SchemaExpansion()
    {
      active.erase(name);
    }

    SchemaExpansion(const SchemaExpansion&) = delete;
    SchemaExpansion& operator=(const SchemaExpansion&) = delete;
  };

  struct SchemaDocument
  {
    Json document = Json{{"components", {{"schemas", Json::object()}}}};
    std::set<std::string> generating = {};

    template <typename T>
    Json add_schema_component();

    template <typename T>
    Json add_required_schema_component();
  };

  template <typename T>
  concept AdlSchemaFill =
    requires(Json& schema, const T* tag) { fill_json_schema(schema, tag); };

  template <typename T>
  concept AdlSchemaComponents =
    requires(SchemaDocument& document, Json& schema, const T* tag) {
      add_schema_components(document, schema, tag);
    };

  // Glaze's schema model cannot represent arbitrary custom keywords or formats.
  // Collect exact ADL schemas separately rather than dropping those
  // constraints. A scoped, thread-local context supports concurrent and nested
  // generation.
  struct SchemaContext
  {
    inline static thread_local SchemaContext* current = nullptr;
    SchemaContext* previous = current;
    Json definitions = Json::object();
    SchemaDocument* document = nullptr;
    std::map<std::string, std::set<std::type_index>> native_types;
    std::map<std::string, std::set<std::type_index>> adl_types;

    SchemaContext()
    {
      current = this;
    }

    ~SchemaContext()
    {
      current = previous;
    }

    SchemaContext(const SchemaContext&) = delete;
    SchemaContext& operator=(const SchemaContext&) = delete;
  };

  inline std::string escape_pointer(std::string_view value)
  {
    std::string result;
    for (const auto c : value)
    {
      if (c == '~')
      {
        result += "~0";
      }
      else if (c == '/')
      {
        result += "~1";
      }
      else
      {
        result += c;
      }
    }
    return result;
  }

  template <DomAdapter T>
  const std::string& adl_schema_reference()
  {
    static const std::string name =
      "ccf.adl." + ccf::ds::json::schema_name<T>();
    static const std::string reference = "#/$defs/" + escape_pointer(name);
    auto* context = SchemaContext::current;
    if (context == nullptr)
    {
      throw std::logic_error("CCF ADL schemas require ccf::json::schema<T>()");
    }
    if (context->adl_types[name].insert(typeid(T)).second)
    {
      auto schema = Json::object();
      if constexpr (std::is_enum_v<T>)
      {
        fill_enum_schema(schema, static_cast<const T*>(nullptr));
      }
      else if constexpr (AdlLeaf<T>)
      {
        if constexpr (AdlSchemaComponents<T>)
        {
          schema = context->document->template add_schema_component<T>();
        }
        else if constexpr (AdlSchemaFill<T>)
        {
          fill_json_schema(schema, static_cast<const T*>(nullptr));
        }
        else
        {
          static_assert(
            AdlSchemaComponents<T>,
            "Custom JSON types require ADL fill_json_schema or "
            "add_schema_components");
        }
      }
      else
      {
        ccf::ds::json::fill_schema<T>(schema);
      }
      try
      {
        auto definition = rebase_schema(schema, reference, true);
        const auto existing = context->definitions.find(name);
        if (existing != context->definitions.end() && *existing != definition)
        {
          throw std::logic_error("Conflicting schema definition: " + name);
        }
        context->definitions[name] = std::move(definition);
      }
      catch (const std::logic_error& error)
      {
        throw std::logic_error(
          "Invalid ADL schema for " + name + ": " + error.what());
      }
    }
    return reference;
  }

  template <DomAdapter T>
  struct AdlSchema
  {
    template <auto Options>
    static void op(auto& schema, [[maybe_unused]] auto& definitions)
    {
      schema.ref = adl_schema_reference<T>();
    }
  };

  template <typename T, auto Options = schema_opts>
  Json native_schema(SchemaDocument* shared_document = nullptr)
  {
    SchemaContext context;
    SchemaDocument document;
    context.document = shared_document != nullptr ? shared_document : &document;
    // Keep definitions until CCF can share them across the entire document.
    glz::schema native{};
    native.defs.emplace();
    glz::detail::to_json_schema<T>::template op<Options>(native, *native.defs);
    const auto result = glz::write<Options>(native);
    if (!result)
    {
      throw std::logic_error(glz::format_error(result.error(), ""));
    }
    auto schema = Json::parse(*result);
    if (schema.contains("$defs"))
    {
      const auto root_definition =
        schema["$defs"].find(std::string(glz::name_v<T>));
      if (root_definition != schema["$defs"].end())
      {
        auto root = schema;
        root.erase("$defs");
        if (root != *root_definition)
        {
          throw std::logic_error(
            "Conflicting schema definition: " + std::string(glz::name_v<T>));
        }
      }
    }
    for (const auto& [name, definition] : context.definitions.items())
    {
      schema["$defs"][name] = std::move(definition);
    }
    if (shared_document == nullptr)
    {
      for (const auto& [name, definition] :
           document.document["components"]["schemas"].items())
      {
        schema["$defs"]["ccf.openapi." + name] = definition;
      }
      schema = transform_schema(std::move(schema), [](Json node) {
        if (node.contains("$ref"))
        {
          const auto reference = node.at("$ref").get<std::string>();
          constexpr std::string_view prefix = "#/components/schemas/";
          if (reference.starts_with(prefix))
          {
            node["$ref"] =
              "#/$defs/ccf.openapi." + reference.substr(prefix.size());
          }
        }
        return node;
      });
    }
    return schema;
  }

  template <typename T>
  Json standalone_schema()
  {
    try
    {
      const auto schema = prepare_schema(native_schema<T>());
      std::set<std::string> expanding;
      return inline_references(schema, schema, expanding);
    }
    catch (const Json::out_of_range& error)
    {
      throw std::logic_error(
        "Invalid standalone schema for '" + std::string(glz::name_v<T>) +
        "': " + error.what());
    }
  }

  inline bool primitive_definition(std::string_view name)
  {
    return [&]<typename... T>([[maybe_unused]] std::tuple<T...> types) {
      return ((name == glz::name_v<T>) || ...);
    }(std::tuple<
             bool,
             char,
             signed char,
             unsigned char,
             short,
             unsigned short,
             int,
             unsigned int,
             long,
             unsigned long,
             long long,
             unsigned long long,
             float,
             double,
             long double,
             std::string,
             std::string_view,
             std::nullptr_t>{}) ||
      name == "ccf.adl.json" || name == "ccf.adl.json_schema";
  }

  class ComponentSchemas
  {
    const Json& source;
    const std::string& root_name;
    std::string_view type_name;
    Json& components;
    std::map<std::string, std::string> names;
    std::set<std::string> expanding;

    static Json with_constraints(Json target, Json node)
    {
      if (node.empty())
      {
        return target;
      }
      if (target.is_object())
      {
        bool compatible = true;
        for (const auto& [key, value] : target.items())
        {
          compatible &= !node.contains(key) || node[key] == value;
        }
        if (compatible)
        {
          node.update(target);
          return node;
        }
      }
      return Json{{"allOf", Json::array({target, node})}};
    }

    Json resolve(std::string reference)
    {
      auto target = source.at(Json::json_pointer(reference.substr(1)));
      std::set<std::string> aliases;
      while (target.is_object() && target.size() == 1 &&
             target.contains("$ref"))
      {
        if (!aliases.insert(reference).second)
        {
          throw std::logic_error("Cyclic schema alias: " + reference);
        }
        reference = target.at("$ref").get<std::string>();
        if (!reference.starts_with("#/$defs/"))
        {
          return target;
        }
        target = source.at(Json::json_pointer(reference.substr(1)));
      }
      const auto found = names.find(reference);
      const auto original_name =
        found == names.end() ? reference.substr(8) : found->second;
      if (primitive_definition(original_name))
      {
        return rewrite(std::move(target));
      }
      auto name = original_name;
      for (const auto* prefix : {"ccf.adl.", "ccf.openapi."})
      {
        if (name.starts_with(prefix))
        {
          name.erase(0, std::string_view(prefix).size());
          break;
        }
      }
      const auto key =
        original_name == type_name ? root_name : component_name(name);
      if (original_name != type_name && !expanding.contains(reference))
      {
        expanding.insert(reference);
        bool anchored = false;
        transform_schema(target, [&](Json node) {
          anchored |= node.contains("$anchor");
          return node;
        });
        auto definition = rewrite(std::move(target), anchored);
        expanding.erase(reference);
        const auto existing = components.find(key);
        if (existing != components.end() && *existing != definition)
        {
          throw std::logic_error("Conflicting schema component: " + key);
        }
        components[key] = std::move(definition);
      }
      return Json{{"$ref", "#/components/schemas/" + key}};
    }

  public:
    ComponentSchemas(
      const Json& source_,
      const std::string& root_name_,
      std::string_view type_name_,
      Json& components_) :
      source(source_),
      root_name(root_name_),
      type_name(type_name_),
      components(components_)
    {
      const auto definitions = source.value("$defs", Json::object());
      for (const auto& [name, definition] : definitions.items())
      {
        names.emplace("#/$defs/" + escape_pointer(name), name);
      }
    }

    Json rewrite(Json schema, bool preserve_local_definitions = false)
    {
      if (schema.is_boolean())
      {
        return schema;
      }
      if (!preserve_local_definitions)
      {
        schema.erase("$defs");
      }
      return transform_schema(std::move(schema), [&](Json node) {
        if (node.is_object() && !preserve_local_definitions)
        {
          node.erase("$defs");
        }
        if (!node.contains("$ref"))
        {
          return node;
        }
        const auto reference = node.at("$ref").get<std::string>();
        if (!reference.starts_with("#/$defs/"))
        {
          return node;
        }
        node.erase("$ref");
        return with_constraints(resolve(reference), std::move(node));
      });
    }
  };

  template <typename T>
  Json component_schema(
    std::string_view name, SchemaDocument* document = nullptr)
  {
    const auto key = component_name(name);
    try
    {
      auto schema = prepare_schema(native_schema<T>(document));
      if (document == nullptr)
      {
        return rebase_schema(schema, "#/components/schemas/" + key);
      }
      return ComponentSchemas(
               schema,
               key,
               glz::name_v<T>,
               document->document["components"]["schemas"])
        .rewrite(schema);
    }
    catch (const Json::out_of_range& error)
    {
      throw std::logic_error(
        "Invalid schema component '" + key + "': " + error.what());
    }
  }

  template <typename T>
  Json SchemaDocument::add_schema_component()
  {
    if constexpr (
      std::is_arithmetic_v<T> || std::is_same_v<T, std::string> ||
      std::is_same_v<T, Json> || std::is_same_v<T, ccf::ds::json::JsonSchema>)
    {
      return ccf::ds::json::schema_element<T>();
    }
    const auto name = component_name(ccf::ds::json::schema_name<T>());
    auto& schemas = document["components"]["schemas"];
    if (generating.contains(name))
    {
      return Json{{"$ref", "#/components/schemas/" + name}};
    }
    SchemaExpansion expansion(generating, name);
    auto schema = Json::object();
    {
      if constexpr (Declared<T>)
      {
        schema = component_schema<T>(name, this);
      }
      else if constexpr (AdlSchemaComponents<T>)
      {
        add_schema_components(*this, schema, static_cast<const T*>(nullptr));
        schema = rebase_schema(schema, "#/components/schemas/" + name, true);
      }
      else if constexpr (AdlSchemaFill<T>)
      {
        fill_json_schema(schema, static_cast<const T*>(nullptr));
        schema = rebase_schema(schema, "#/components/schemas/" + name, true);
      }
      else if constexpr (DomAdapter<T>)
      {
        ccf::ds::json::fill_schema<T>(schema);
      }
      else
      {
        schema = component_schema<T>(name, this);
      }
    }
    const auto existing = schemas.find(name);
    if (existing != schemas.end() && *existing != schema)
    {
      throw std::logic_error("Conflicting schema component: " + name);
    }
    schemas[name] = std::move(schema);
    return Json{{"$ref", "#/components/schemas/" + name}};
  }

  template <typename T>
  Json SchemaDocument::add_required_schema_component()
  {
    if constexpr (ccf::nonstd::is_specialization<T, std::optional>::value)
    {
      return Json{
        {"anyOf",
         Json::array(
           {add_schema_component<typename T::value_type>(),
            Json{{"type", "null"}}})}};
    }
    else
    {
      return add_schema_component<T>();
    }
  }

}

namespace glz::detail
{
  template <typename T, auto Options>
  void describe_ccf_schema(glz::schema& schema, auto& definitions)
  {
    if constexpr (
      std::is_arithmetic_v<T> || std::is_same_v<T, std::string> ||
      std::is_same_v<T, std::nullptr_t>)
    {
      to_json_schema<T>::template op<Options>(schema, definitions);
    }
    else
    {
      validate_ref<name_v<T>>();
      auto& definition = definitions[name_v<T>];
      auto* context = ccf::json::detail::SchemaContext::current;
      if (context == nullptr)
      {
        throw std::logic_error("CCF schemas require ccf::json::schema<T>()");
      }
      auto& types = context->native_types[std::string(name_v<T>)];
      if (types.insert(typeid(T)).second)
      {
        glz::schema candidate;
        to_json_schema<T>::template op<Options>(candidate, definitions);
        const auto serialise = [](const glz::schema& value) {
          const auto result = glz::write<Options>(value);
          if (!result)
          {
            throw std::logic_error(glz::format_error(result.error(), ""));
          }
          return ccf::json::detail::Json::parse(*result);
        };
        if (types.size() > 1 && serialise(candidate) != serialise(definition))
        {
          throw std::logic_error(
            "Conflicting schema definition: " + std::string(name_v<T>));
        }
        definition = std::move(candidate);
      }
      schema.ref = join_v<chars<"#/$defs/">, name_v<T>>;
    }
  }

  template <>
  struct to_json_schema<char>
  {
    template <auto Options>
    static void op(auto& schema, auto&)
    {
      schema.type = sv{"integer"};
      schema.minimum = int64_t{std::numeric_limits<char>::min()};
      schema.maximum = uint64_t{std::numeric_limits<char>::max()};
    }
  };

  template <typename T, auto Options>
  void describe_ccf_array(auto& schema, auto& definitions)
  {
    schema.type = sv{"array"};
    if constexpr (has_fixed_size_container<T>)
    {
      schema.maxItems = get_size<T>();
    }
    auto items = std::make_shared<glz::schema>();
    describe_ccf_schema<std::decay_t<range_value_t<T>>, Options>(
      *items, definitions);
    schema.items = std::move(items);
  }

  template <>
  struct to_json_schema<std::vector<char>>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      describe_ccf_array<std::vector<char>, Options>(schema, definitions);
    }
  };

  template <size_t N>
  struct to_json_schema<std::array<char, N>>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      describe_ccf_array<std::array<char, N>, Options>(schema, definitions);
      schema.minItems = N;
    }
  };

  template <ccf::json::detail::Declared T>
    requires glaze_object_t<T>
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      schema.type = sv{"object"};
      schema.properties = std::map<sv, glz::schema, std::less<>>{};
      std::apply(
        [&](auto... field) {
          const auto add = [&](auto f) {
            using F = decltype(f);
            using Member =
              std::remove_cvref_t<decltype(std::declval<T>().*F::member)>;
            auto& property = (*schema.properties)[f.name];
            if constexpr (F::policy == ccf::json::detail::Presence::Required)
            {
              if (!schema.required)
              {
                schema.required = std::vector<sv>{};
              }
              schema.required->push_back(f.name);
            }
            if constexpr (ccf::nonstd::
                            is_specialization<Member, std::optional>::value)
            {
              property.anyOf = std::vector<glz::schema>(2);
              describe_ccf_schema<typename Member::value_type, Options>(
                (*property.anyOf)[0], definitions);
              (*property.anyOf)[1].type = sv{"null"};
            }
            else
            {
              describe_ccf_schema<Member, Options>(property, definitions);
            }
            if constexpr (json_schema_t<T>)
            {
              static const auto annotations = json_schema_type<T>{};
              for_each<reflect<json_schema_type<T>>::size>([&]<auto I>() {
                if (reflect<json_schema_type<T>>::keys[I] == f.name)
                {
                  auto annotation = glz::get<I>(to_tie(annotations));
                  merge_schema_attrs(property, annotation);
                }
              });
            }
          };
          (add(field), ...);
        },
        ccf::json::detail::Metadata<T>::declaration.fields);
    }
  };

  template <is_variant T>
    requires(!ccf::json::detail::DomAdapter<T>)
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      // CCF writes untagged alternatives, which need not be disjoint.
      schema.anyOf = std::vector<glz::schema>(std::variant_size_v<T>);
      glz::for_each<std::variant_size_v<T>>([&]<auto I>() {
        using Alternative = std::variant_alternative_t<I, T>;
        describe_ccf_schema<Alternative, Options>(
          (*schema.anyOf)[I], definitions);
      });
    }
  };

  template <array_t T>
    requires(!ccf::json::detail::DomAdapter<T>)
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      describe_ccf_array<T, Options>(schema, definitions);
    }
  };

  template <nullable_t T>
    requires(!ccf::json::detail::DomAdapter<T>)
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      schema.anyOf = std::vector<glz::schema>(2);
      using Value = std::decay_t<decltype(*std::declval<T>())>;
      describe_ccf_schema<Value, Options>((*schema.anyOf)[0], definitions);
      (*schema.anyOf)[1].type = sv{"null"};
    }
  };

  template <writable_map_t T>
    requires(!ccf::json::detail::DomAdapter<T>)
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      schema.type = sv{"object"};
      auto values = std::make_shared<glz::schema>();
      describe_ccf_schema<typename T::mapped_type, Options>(
        *values, definitions);
      schema.additionalProperties = std::move(values);
    }
  };

  template <auto Options, typename... T>
  void describe_ccf_tuple(auto& schema, auto& definitions)
  {
    schema.type = sv{"array"};
    schema.minItems = sizeof...(T);
    schema.maxItems = sizeof...(T);
    schema.items = false;
    if constexpr (sizeof...(T) > 0)
    {
      schema.prefixItems = std::vector<glz::schema>(sizeof...(T));
      size_t index = 0;
      (describe_ccf_schema<T, Options>(
         (*schema.prefixItems)[index++], definitions),
       ...);
    }
  }

  template <typename First, typename Second>
  struct to_json_schema<std::pair<First, Second>>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      describe_ccf_tuple<Options, First, Second>(schema, definitions);
    }
  };

  template <typename... T>
  struct to_json_schema<std::tuple<T...>>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      describe_ccf_tuple<Options, T...>(schema, definitions);
    }
  };

  template <ccf::json::detail::DomAdapter T>
    requires(!array_t<T> && !nullable_t<T> && !ccf::json::detail::legacy_map<T>)
  struct to_json_schema<T> : ccf::json::detail::AdlSchema<T>
  {};

  template <ccf::json::detail::DomAdapter T>
    requires nullable_t<T>
  struct to_json_schema<T> : ccf::json::detail::AdlSchema<T>
  {};

  template <ccf::json::detail::DomAdapter T>
    requires(array_t<T> && !ccf::json::detail::legacy_map<T>)
  struct to_json_schema<T> : ccf::json::detail::AdlSchema<T>
  {};

  template <ccf::json::detail::DomAdapter T>
    requires(array_t<T> && ccf::json::detail::legacy_map<T>)
  struct to_json_schema<T>
  {
    template <auto Options>
    static void op(auto& schema, auto& definitions)
    {
      if constexpr (nlohmann::detail::
                      is_compatible_object_type<nlohmann::json, T>::value)
      {
        schema.type = sv{"object"};
        auto values = std::make_shared<glz::schema>();
        describe_ccf_schema<typename T::mapped_type, Options>(
          *values, definitions);
        schema.additionalProperties = std::move(values);
      }
      else
      {
        schema.type = sv{"array"};
        auto pair = std::make_shared<glz::schema>();
        describe_ccf_tuple<
          Options,
          typename T::key_type,
          typename T::mapped_type>(*pair, definitions);
        schema.items = std::move(pair);
      }
    }
  };
}
