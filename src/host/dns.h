// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "../ds/logger.h"

#include <uv.h>

namespace asynchost
{
  class DNS
  {
  public:
    static bool resolve_sync(
      const std::string& host,
      const std::string& service,
      void* ud,
      uv_getaddrinfo_cb cb)
    {
      struct addrinfo hints;
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
      hints.ai_flags = 0;

      auto resolver = new uv_getaddrinfo_t;
      resolver->data = ud;

      int rc;

      if (
        (rc = uv_getaddrinfo(
           uv_default_loop(),
           resolver,
           nullptr,
           host.c_str(),
           service.c_str(),
           &hints)) < 0)
      {
        LOG_FAIL_FMT(
          "uv_getaddrinfo for host:service [{}:{}] failed with error {}",
          host,
          service,
          uv_strerror(rc));
        delete resolver;
        return false;
      }

      cb(resolver, rc, &hints);

      return true;
    }

    static void sleep_rand(uv_work_t* req)
    {
      const auto sleep_time = 100 + (rand() % 500);
      LOG_INFO_FMT(
        "Pausing {}ms before resolving, on thread {}",
        sleep_time,
        std::this_thread::get_id());
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }

    struct AsyncResolveArgs
    {
      std::string host;
      std::string service;
      void* ud;
      uv_getaddrinfo_cb cb;
      uv_getaddrinfo_t* resolver;
    };

    static void async_resolve(uv_work_t* req, int status)
    {
      struct addrinfo hints;
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
      hints.ai_flags = 0;

      AsyncResolveArgs* args = (AsyncResolveArgs*)req->data;

      int rc;
      if (
        (rc = uv_getaddrinfo(
           uv_default_loop(),
           args->resolver,
           args->cb,
           args->host.c_str(),
           args->service.c_str(),
           &hints)) < 0)
      {
        LOG_FAIL_FMT(
          "uv_getaddrinfo for host:service [{}:{}] failed (async) with error "
          "{}",
          args->host,
          args->service,
          uv_strerror(rc));
        delete args->resolver;
      }

      delete args;
      delete req;
    }

    static bool resolve_async(
      const std::string& host,
      const std::string& service,
      void* ud,
      uv_getaddrinfo_cb cb,
      uv_getaddrinfo_t*& resolver)
    {
      resolver = new uv_getaddrinfo_t;
      resolver->data = ud;

      {
        uv_work_t* work_req = new uv_work_t;
        {
          auto req_data = new AsyncResolveArgs;
          req_data->host = host;
          req_data->service = service;
          req_data->ud = ud;
          req_data->cb = cb;
          req_data->resolver = resolver;
          work_req->data = req_data;
        }
        uv_queue_work(uv_default_loop(), work_req, sleep_rand, async_resolve);
      }

      return true;
    }
  };
}
