// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "crypto/pem.h"
#include "service_map.h"

#include <nlohmann/json.hpp>

namespace ccf
{
  struct NewUser
  {
    crypto::Pem cert;
    nlohmann::json user_data = nullptr;
  };
  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(NewUser)
  DECLARE_JSON_REQUIRED_FIELDS(NewUser, cert)
  DECLARE_JSON_OPTIONAL_FIELDS(NewUser, user_data)

  struct UserDetails
  {
    /** Free-form user data, useful to store role information about users for
        example. */
    nlohmann::json user_data = nullptr;
  };
  DECLARE_JSON_TYPE(UserDetails)
  DECLARE_JSON_REQUIRED_FIELDS(UserDetails, user_data)

  struct UserCerts : public kv::RawCopySerialisedMap<UserId, crypto::Pem>
  {
    static std::string name()
    {
      return "public:ccf.gov.users.certs";
    }
  };

  using UserInfo = ServiceMap<UserId, UserDetails>;
}