// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ds/ccf_deprecated.h"
#include "ds/json_schema.h"
#include "enclave/rpc_context.h"
#include "http/http_consts.h"
#include "http/ws_consts.h"
#include "kv/store.h"
#include "kv/tx.h"
#include "node/certs.h"
#include "serialization.h"

#include <functional>
#include <http-parser/http_parser.h>
#include <nlohmann/json.hpp>
#include <set>

namespace ccf
{
  struct EndpointContext
  {
    std::shared_ptr<enclave::RpcContext> rpc_ctx;
    kv::Tx& tx;
    CallerId caller_id;
  };
  using EndpointFunction = std::function<void(EndpointContext& ctx)>;

  using RequestArgs CCF_DEPRECATED(
    "Handlers have been renamed to Endpoints. Please use EndpointContext "
    "instead of HandlerArgs, and use 'auto' wherever possible") =
    EndpointContext;

  // Read-only endpoints can only get values from the kv, they cannot write
  struct ReadOnlyEndpointContext
  {
    std::shared_ptr<enclave::RpcContext> rpc_ctx;
    kv::ReadOnlyTx& tx;
    CallerId caller_id;
  };
  using ReadOnlyEndpointFunction =
    std::function<void(ReadOnlyEndpointContext& ctx)>;

  // Commands are endpoints which do not interact with the kv, even to read
  struct CommandEndpointContext
  {
    std::shared_ptr<enclave::RpcContext> rpc_ctx;
    CallerId caller_id;
  };
  using CommandEndpointFunction =
    std::function<void(CommandEndpointContext& ctx)>;

  enum class ForwardingRequired
  {
    Sometimes,
    Always,
  };

  /** The EndpointRegistry records the user-defined endpoints for a given
   * CCF application.
   */
  class EndpointRegistry
  {
  public:
    enum ReadWrite
    {
      Read,
      Write
    };

    /** An Endpoint represents a user-defined resource that can be invoked by
     * authorised users via HTTP requests, over TLS. An Endpoint is accessible
     * at a specific verb and URI, e.g. POST /app/accounts or GET /app/records.
     *
     * Endpoints can read from and mutate the state of the replicated key-value
     * store.
     *
     * A CCF application is a collection of Endpoints recorded in the
     * application's EndpointRegistry.
     */
    struct Endpoint
    {
      std::string method;
      EndpointFunction func;
      EndpointRegistry* registry = nullptr;

      nlohmann::json params_schema = nullptr;

      /** Sets the JSON schema that the request parameters must comply with.
       *
       * @param j Request parameters JSON schema
       * @return This Endpoint for further modification
       */
      Endpoint& set_params_schema(const nlohmann::json& j)
      {
        params_schema = j;
        return *this;
      }

      nlohmann::json result_schema = nullptr;

      /** Sets the JSON schema that the request response must comply with.
       *
       * @param j Request response JSON schema
       * @return This Endpoint for further modification
       */
      Endpoint& set_result_schema(const nlohmann::json& j)
      {
        result_schema = j;
        return *this;
      }

      /** Sets the schema that the request parameters and response must comply
       * with based on JSON-serialisable data structures.
       *
       * \verbatim embed:rst:leading-asterisk
       * .. note::
       *  See ``DECLARE_JSON_`` serialisation macros for serialising
       *  user-defined data structures.
       * \endverbatim
       *
       * @tparam In Request parameters JSON-serialisable data structure
       * @tparam Out Request response JSON-serialisable data structure
       * @return This Endpoint for further modification
       */
      template <typename In, typename Out>
      Endpoint& set_auto_schema()
      {
        if constexpr (!std::is_same_v<In, void>)
        {
          params_schema = ds::json::build_schema<In>(method + "/params");
        }
        else
        {
          params_schema = nullptr;
        }

        if constexpr (!std::is_same_v<Out, void>)
        {
          result_schema = ds::json::build_schema<Out>(method + "/result");
        }
        else
        {
          result_schema = nullptr;
        }

        return *this;
      }

      /** Sets the schema that the request parameters and response must comply
       * with, based on a single JSON-serialisable data structure.
       *
       * \verbatim embed:rst:leading-asterisk
       * .. note::
       *   ``T`` data structure should contain two nested ``In`` and ``Out``
       *   structures for request parameters and response format, respectively.
       * \endverbatim
       *
       * @tparam T Request parameters and response JSON-serialisable data
       * structure
       * @return This Endpoint for further modification
       */
      template <typename T>
      Endpoint& set_auto_schema()
      {
        return set_auto_schema<typename T::In, typename T::Out>();
      }

      ForwardingRequired forwarding_required = ForwardingRequired::Always;

      /** Overrides whether a Endpoint is always forwarded, or whether it is
       * safe to sometimes execute on followers.
       *
       * @param fr Enum value with desired status
       * @return This Endpoint for further modification
       */
      Endpoint& set_forwarding_required(ForwardingRequired fr)
      {
        forwarding_required = fr;
        return *this;
      }

      CCF_DEPRECATED("Replaced by set_forwarding_required")
      Endpoint& set_read_write(ReadWrite rw)
      {
        return set_forwarding_required(
          rw == Read ? ForwardingRequired::Sometimes :
                       ForwardingRequired::Always);
      }

      bool require_client_signature = false;

      /** Requires that the HTTP request is cryptographically signed by
       * the calling user.
       *
       * By default, client signatures are not required.
       *
       * @param v Boolean indicating whether the request must be signed
       * @return This Endpoint for further modification
       */
      Endpoint& set_require_client_signature(bool v)
      {
        require_client_signature = v;
        return *this;
      }

      bool require_client_identity = true;

      /** Requires that the HTTPS request is emitted by a user whose public
       * identity has been registered in advance by consortium members.
       *
       * By default, a known client identity is required.
       *
       * \verbatim embed:rst:leading-asterisk
       * .. warning::
       *  If set to false, it is left to the application developer to implement
       *  the authentication and authorisation mechanisms for the Endpoint.
       * \endverbatim
       *
       * @param v Boolean indicating whether the user identity must be known
       * @return This Endpoint for further modification
       */
      Endpoint& set_require_client_identity(bool v)
      {
        if (!v && registry != nullptr && !registry->has_certs())
        {
          LOG_INFO_FMT(
            "Disabling client identity requirement on {} Endpoint has no "
            "effect "
            "since its registry does not have certificates table",
            method);
          return *this;
        }

        require_client_identity = v;
        return *this;
      }

      bool execute_locally = false;

      /** Indicates that the execution of the Endpoint does not require
       * consensus from other nodes in the network.
       *
       * By default, endpoints are not executed locally.
       *
       * \verbatim embed:rst:leading-asterisk
       * .. warning::
       *  Use with caution. This should only be used for non-critical endpoints
       *  that do not read or mutate the state of the key-value store.
       * \endverbatim
       *
       * @param v Boolean indicating whether the Endpoint is executed locally,
       * on the node receiving the request
       * @return This Endpoint for further modification
       */
      Endpoint& set_execute_locally(bool v)
      {
        execute_locally = v;
        return *this;
      }

      RESTVerb verb = HTTP_POST;

      CCF_DEPRECATED(
        "HTTP Verb should not be changed after installation: pass verb to "
        "install()")
      Endpoint& set_allowed_verb(RESTVerb v)
      {
        const auto previous_verb = verb;
        return registry->reinstall(*this, method, previous_verb);
      }

      CCF_DEPRECATED(
        "HTTP Verb should not be changed after installation: use "
        "install(...HTTP_GET...)")
      Endpoint& set_http_get_only()
      {
        return set_allowed_verb(HTTP_GET);
      }

      CCF_DEPRECATED(
        "HTTP Verb should not be changed after installation: use "
        "install(...HTTP_POST...)")
      Endpoint& set_http_post_only()
      {
        return set_allowed_verb(HTTP_POST);
      }

      /** Finalise and install this endpoint
       */
      void install()
      {
        registry->install(*this);
      }
    };

  protected:
    std::optional<Endpoint> default_handler;
    std::map<std::string, std::map<RESTVerb, Endpoint>> installed_handlers;

    kv::Consensus* consensus = nullptr;
    kv::TxHistory* history = nullptr;

    Certs* certs = nullptr;

  public:
    EndpointRegistry(
      kv::Store& tables, const std::string& certs_table_name = "")
    {
      if (!certs_table_name.empty())
      {
        certs = tables.get<Certs>(certs_table_name);
      }
    }

    virtual ~EndpointRegistry() {}

    /** Create a new endpoint.
     *
     * Caller should set any additional properties on the returned Endpoint
     * object, and finally call Endpoint::install() to install it.
     *
     * @param method The URI at which this endpoint will be installed
     * @param verb The HTTP verb which this endpoint will respond to
     * @param f Functor which will be invoked for requests to VERB /method
     * @return The new Endpoint for further modification
     */
    Endpoint make_endpoint(
      const std::string& method, RESTVerb verb, const EndpointFunction& f)
    {
      Endpoint endpoint;
      endpoint.method = method;
      endpoint.verb = verb;
      endpoint.func = f;
      // By default, all write transactions are forwarded
      endpoint.forwarding_required = ForwardingRequired::Always;
      endpoint.registry = this;
      return endpoint;
    }

    /** Create a read-only endpoint.
     */
    Endpoint make_read_only_endpoint(
      const std::string& method,
      RESTVerb verb,
      const ReadOnlyEndpointFunction& f)
    {
      return make_endpoint(
               method,
               verb,
               [f](EndpointContext& ctx) {
                 ReadOnlyEndpointContext ro_ctx{
                   ctx.rpc_ctx, ctx.tx, ctx.caller_id};
                 f(ro_ctx);
               })
        .set_forwarding_required(ForwardingRequired::Sometimes);
    }

    /** Create a new command endpoint.
     *
     * Commands are endpoints which do not read or write from the KV. See
     * make_endpoint().
     */
    Endpoint make_command_endpoint(
      const std::string& method,
      RESTVerb verb,
      const CommandEndpointFunction& f)
    {
      return make_endpoint(
               method,
               verb,
               [f](EndpointContext& ctx) {
                 CommandEndpointContext command_ctx{ctx.rpc_ctx, ctx.caller_id};
                 f(command_ctx);
               })
        .set_forwarding_required(ForwardingRequired::Sometimes);
    }

    /** Install the given endpoint, using its method and verb
     *
     * If an implementation is already installed for this method and verb, it
     * will be replaced.
     * @param endpoint Endpoint object describing the new resource to install
     */
    void install(const Endpoint& endpoint)
    {
      installed_handlers[endpoint.method][endpoint.verb] = endpoint;
    }

    CCF_DEPRECATED(
      "HTTP verb should be specified explicitly. Use: "
      "make_endpoint(METHOD, VERB, FN)"
      "  .set_forwarding_required() // Optional"
      "  .install()"
      "or make_read_only_endpoint(...")
    Endpoint& install(
      const std::string& method,
      const EndpointFunction& f,
      ReadWrite read_write)
    {
      constexpr auto default_verb = HTTP_POST;
      make_endpoint(method, default_verb, f)
        .set_read_write(read_write)
        .install();
      return installed_handlers[method][default_verb];
    }

    // Only needed to support deprecated functions
    Endpoint& reinstall(
      const Endpoint& h, const std::string& prev_method, RESTVerb prev_verb)
    {
      const auto handlers_it = installed_handlers.find(prev_method);
      if (handlers_it != installed_handlers.end())
      {
        handlers_it->second.erase(prev_verb);
      }
      return installed_handlers[h.method][h.verb] = h;
    }

    /** Set a default EndpointFunction
     *
     * The default EndpointFunction is only invoked if no specific
     * EndpointFunction was found.
     *
     * @param f Method implementation
     * @return This Endpoint for further modification
     */
    Endpoint& set_default(EndpointFunction f)
    {
      default_handler = {"", f, this};
      return default_handler.value();
    }

    /** Populate out with all supported methods
     *
     * This is virtual since the default endpoint may do its own dispatch
     * internally, so derived implementations must be able to populate the list
     * with the supported methods however it constructs them.
     */
    virtual void list_methods(kv::Tx& tx, ListMethods::Out& out)
    {
      for (const auto& [method, verb_handlers] : installed_handlers)
      {
        out.methods.push_back(method);
      }
    }

    virtual void init_handlers(kv::Store& tables) {}

    virtual Endpoint* find_endpoint(const std::string& method, RESTVerb verb)
    {
      auto search = installed_handlers.find(method);
      if (search != installed_handlers.end())
      {
        auto& verb_handlers = search->second;
        auto search2 = verb_handlers.find(verb);
        if (search2 != verb_handlers.end())
        {
          return &search2->second;
        }
      }

      if (default_handler)
      {
        return &default_handler.value();
      }

      return nullptr;
    }

    virtual std::vector<RESTVerb> get_allowed_verbs(const std::string& method)
    {
      std::vector<RESTVerb> verbs;
      auto search = installed_handlers.find(method);
      if (search != installed_handlers.end())
      {
        for (auto& [verb, endpoint] : search->second)
        {
          verbs.push_back(verb);
        }
      }

      return verbs;
    }

    virtual void tick(
      std::chrono::milliseconds elapsed, kv::Consensus::Statistics stats)
    {}

    bool has_certs()
    {
      return certs != nullptr;
    }

    virtual CallerId get_caller_id(
      kv::Tx& tx, const std::vector<uint8_t>& caller)
    {
      if (certs == nullptr || caller.empty())
      {
        return INVALID_ID;
      }

      auto certs_view = tx.get_view(*certs);
      auto caller_id = certs_view->get(caller);

      if (!caller_id.has_value())
      {
        return INVALID_ID;
      }

      return caller_id.value();
    }

    void set_consensus(kv::Consensus* c)
    {
      consensus = c;
    }

    void set_history(kv::TxHistory* h)
    {
      history = h;
    }
  };
}