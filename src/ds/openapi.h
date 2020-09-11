// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/json.h"
#include "ds/nonstd.h"

#include <http-parser/http_parser.h>
#include <nlohmann/json.hpp>
#include <string>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"

namespace ds
{
  /**
   * This namespace contains helper functions, structs, and templates for
   * modifying an OpenAPI JSON document. They do not set every field, but should
   * fill every _required_ field, and the resulting object can be further
   * modified by hand as required.
   */
  namespace openapi
  {
    namespace access
    {
      static inline nlohmann::json& get_object(
        nlohmann::json& j, const std::string& k)
      {
        const auto ib = j.emplace(k, nlohmann::json::object());
        return ib.first.value();
      }

      static inline nlohmann::json& get_array(
        nlohmann::json& j, const std::string& k)
      {
        const auto ib = j.emplace(k, nlohmann::json::array());
        return ib.first.value();
      }
    }

    static inline void check_path_valid(const std::string& s)
    {
      if (s.rfind("/", 0) != 0)
      {
        throw std::logic_error(
          fmt::format("'{}' is not a valid path - must begin with '/'", s));
      }
    }

    static inline nlohmann::json create_document(
      const std::string& title,
      const std::string& description,
      const std::string& document_version)
    {
      return nlohmann::json{{"openapi", "3.0.0"},
                            {"info",
                             {{"title", title},
                              {"description", description},
                              {"version", document_version}}},
                            {"servers", nlohmann::json::array()},
                            {"paths", nlohmann::json::object()}};
    }

    static inline nlohmann::json& server(
      nlohmann::json& document, const std::string& url)
    {
      auto& servers = access::get_object(document, "servers");
      servers.push_back({{"url", url}});
      return servers.back();
    }

    static inline nlohmann::json& path(
      nlohmann::json& document, const std::string& path)
    {
      check_path_valid(path);
      auto& paths = access::get_object(document, "paths");
      return access::get_object(paths, path);
    }

    static inline nlohmann::json& path_operation(
      nlohmann::json& path, http_method verb)
    {
      // HTTP_GET becomes the string "get"
      std::string s = http_method_str(verb);
      nonstd::to_lower(s);
      return access::get_object(path, s);
    }

    static inline nlohmann::json& parameters(nlohmann::json& path_operation)
    {
      return access::get_array(path_operation, "parameters");
    }

    static inline nlohmann::json& response(
      nlohmann::json& path_operation,
      http_status status,
      const std::string& description = "Default response description")
    {
      auto& responses = access::get_object(path_operation, "responses");
      // HTTP_STATUS_OK (aka an int-enum with value 200) becomes the string
      // "200"
      const auto s = std::to_string(status);
      auto& response = access::get_object(responses, s);
      response["description"] = description;
      return response;
    }

    static inline nlohmann::json& request_body(nlohmann::json& path_operation)
    {
      auto& request_body = access::get_object(path_operation, "requestBody");
      access::get_object(request_body, "content");
      return request_body;
    }

    static inline nlohmann::json& media_type(
      nlohmann::json& j, const std::string& mt)
    {
      auto& content = access::get_object(j, "content");
      return access::get_object(content, mt);
    }

    static inline nlohmann::json& schema(nlohmann::json& media_type_object)
    {
      return access::get_object(media_type_object, "schema");
    }

    //
    // Helper functions for auto-inserting schema into components
    //

    static inline nlohmann::json components_ref_object(
      const std::string& element_name)
    {
      auto schema_ref_object = nlohmann::json::object();
      schema_ref_object["$ref"] =
        fmt::format("#/components/schemas/{}", element_name);
      return schema_ref_object;
    }

    // Returns a ref object pointing to the item inserted into the components
    static inline nlohmann::json add_schema_to_components(
      nlohmann::json& document,
      const std::string& element_name,
      const nlohmann::json& schema_)
    {
      auto& components = access::get_object(document, "components");
      auto& schemas = access::get_object(components, "schemas");

      const auto schema_it = schemas.find(element_name);
      if (schema_it != schemas.end())
      {
        // Check that the existing schema matches the new one being added with
        // the same name
        const auto& existing_schema = schema_it.value();
        if (schema_ != existing_schema)
        {
          throw std::logic_error(fmt::format(
            "Adding schema with name '{}'. Does not match previous schema "
            "registered with this name: {} vs {}",
            element_name,
            schema_.dump(),
            existing_schema.dump()));
        }
      }
      else
      {
        schemas.emplace(element_name, schema_);
      }

      return components_ref_object(element_name);
    }

    struct SchemaHelper
    {
      nlohmann::json& document;

      template <typename T>
      nlohmann::json add_schema_component()
      {
        nlohmann::json schema;
        if constexpr (nonstd::is_specialization<T, std::optional>::value)
        {
          return add_schema_component<typename T::value_type>();
        }
        else if constexpr (nonstd::is_specialization<T, std::vector>::value)
        {
          schema["type"] = "array";
          schema["items"] = add_schema_component<typename T::value_type>();

          return add_schema_to_components(
            document, ds::json::schema_name<T>(), schema);
        }
        else if constexpr (
          nonstd::is_specialization<T, std::map>::value ||
          nonstd::is_specialization<T, std::unordered_map>::value)
        {
          // Nlohmann serialises maps to an array of (K, V) pairs
          if (std::is_same<typename T::key_type, std::string>::value)
          {
            // ...unless the keys are strings!
            schema["type"] = "object";
            schema["additionalProperties"] =
              add_schema_component<typename T::mapped_type>();
          }
          else
          {
            schema["type"] = "array";
            auto items = nlohmann::json::object();
            {
              items["type"] = "array";

              auto sub_items = nlohmann::json::array();
              // NB: OpenAPI doesn't like this tuple for "items", even though
              // its valid JSON schema. May need to switch this to oneOf to
              // satisfy some validators
              sub_items.push_back(add_schema_component<typename T::key_type>());
              sub_items.push_back(
                add_schema_component<typename T::mapped_type>());
              items["items"] = sub_items;
            }
            schema["items"] = items;
          }
          return add_schema_to_components(
            document, ds::json::schema_name<T>(), schema);
        }
        else if constexpr (nonstd::is_specialization<T, std::pair>::value)
        {
          schema["type"] = "array";
          auto items = nlohmann::json::array();
          items.push_back(add_schema_component<typename T::first_type>());
          items.push_back(add_schema_component<typename T::second_type>());
          schema["items"] = items;
          return add_schema_to_components(
            document, ds::json::schema_name<T>(), schema);
        }
        else if constexpr (
          std::is_same<T, std::string>::value || std::is_same<T, bool>::value ||
          std::is_same<T, uint8_t>::value || std::is_same<T, uint16_t>::value ||
          std::is_same<T, uint32_t>::value ||
          std::is_same<T, uint64_t>::value || std::is_same<T, int8_t>::value ||
          std::is_same<T, int16_t>::value || std::is_same<T, int32_t>::value ||
          std::is_same<T, int64_t>::value || std::is_same<T, float>::value ||
          std::is_same<T, double>::value ||
          std::is_same<T, nlohmann::json>::value)
        {
          ds::json::fill_schema<T>(schema);
          return add_schema_to_components(
            document, ds::json::schema_name<T>(), schema);
        }
        else
        {
          const auto name = ds::json::adl::schema_name<T>();

          auto& components = access::get_object(document, "components");
          auto& schemas = access::get_object(components, "schemas");

          const auto ib = schemas.emplace(name, nlohmann::json::object());
          if (ib.second)
          {
            auto& j = ib.first.value();
            // Use argument-dependent-lookup to call correct functions

            T t;
            if constexpr (std::is_enum<T>::value)
            {
              fill_enum_schema(j, t);
            }
            else
            {
              add_schema_components(*this, j, t);
            }
          }

          return components_ref_object(name);
        }
      }
    };

    static inline void add_request_body_schema(
      nlohmann::json& document,
      const std::string& uri,
      http_method verb,
      const std::string& content_type,
      const std::string& schema_name,
      const nlohmann::json& schema_)
    {
      check_path_valid(uri);

      auto& rb = request_body(path_operation(path(document, uri), verb));
      rb["description"] = "Auto-generated request body schema";

      schema(media_type(rb, content_type)) =
        add_schema_to_components(document, schema_name, schema_);
    }

    template <typename T>
    static inline void add_request_body_schema(
      nlohmann::json& document,
      const std::string& uri,
      http_method verb,
      const std::string& content_type)
    {
      check_path_valid(uri);

      auto& rb = request_body(path_operation(path(document, uri), verb));
      rb["description"] = "Auto-generated request body schema";

      SchemaHelper sh{document};
      schema(media_type(rb, content_type)) = sh.add_schema_component<T>();
    }

    static inline void add_request_parameter_schema(
      nlohmann::json& document,
      const std::string& uri,
      http_method verb,
      const nlohmann::json& param)
    {
      check_path_valid(uri);

      auto& params = parameters(path_operation(path(document, uri), verb));
      params.push_back(param);
    }

    static inline void add_response_schema(
      nlohmann::json& document,
      const std::string& uri,
      http_method verb,
      http_status status,
      const std::string& content_type,
      const std::string& schema_name,
      const nlohmann::json& schema_)
    {
      check_path_valid(uri);

      auto& r = response(path_operation(path(document, uri), verb), status);

      schema(media_type(r, content_type)) =
        add_schema_to_components(document, schema_name, schema_);
    }

    template <typename T>
    static inline void add_response_schema(
      nlohmann::json& document,
      const std::string& uri,
      http_method verb,
      http_status status,
      const std::string& content_type)
    {
      check_path_valid(uri);

      auto& r = response(path_operation(path(document, uri), verb), status);

      SchemaHelper sh{document};
      schema(media_type(r, content_type)) = sh.add_schema_component<T>();
    }
  }
}

#pragma clang diagnostic pop
