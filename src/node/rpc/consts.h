// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

namespace ccf
{
  struct MemberProcs
  {
    static constexpr auto CREATE = "create";

    static constexpr auto READ = "read";
    static constexpr auto QUERY = "query";

    static constexpr auto COMPLETE = "complete";
    static constexpr auto VOTE = "vote";
    static constexpr auto PROPOSE = "propose";
    static constexpr auto WITHDRAW = "withdraw";

    static constexpr auto ACK = "ack";
    static constexpr auto UPDATE_ACK_STATE_DIGEST = "ack/update_state_digest";

    static constexpr auto GET_ENCRYPTED_RECOVERY_SHARE = "recovery_share";
    static constexpr auto SUBMIT_RECOVERY_SHARE = "recovery_share/submit";
  };

  struct NodeProcs
  {
    static constexpr auto JOIN = "join";
    static constexpr auto GET_SIGNED_INDEX = "signed_index";
    static constexpr auto GET_NODE_QUOTE = "quote";
    static constexpr auto GET_QUOTES = "quotes";
  };
}
