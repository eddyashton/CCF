// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include <memory>

#include "ccf/app_interface.h"

namespace ccfapp
{
  std::unique_ptr<ccf::endpoints::EndpointRegistry> make_user_endpoints_impl(
    ccfapp::AbstractNodeContext& context);
}