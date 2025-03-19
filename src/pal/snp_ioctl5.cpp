// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ccf/pal/snp_ioctl5.h"

#include "ccf/ds/logger.h"

namespace ccf::pal::snp::ioctl5
{
  Attestation::Attestation(const PlatformAttestationReportData& report_data)
  {
    if (report_data.data.size() <= snp_attestation_report_data_size)
    {
      std::copy(
        report_data.data.begin(), report_data.data.end(), req.report_data);
    }
    else
    {
      throw std::logic_error(
        "User-defined report data is larger than available space");
    }

    int fd = open(DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
      throw std::logic_error(fmt::format("Failed to open \"{}\"", DEVICE));
    }

    // Documented at
    // https://www.kernel.org/doc/html/latest/virt/coco/sev-guest.html
    GuestRequest payload = {
      .req_msg_type = MSG_REPORT_REQ,
      .rsp_msg_type = MSG_REPORT_RSP,
      .msg_version = 1,
      .request_len = sizeof(req),
      .request_uaddr = reinterpret_cast<uint64_t>(&req),
      .response_len = sizeof(resp),
      .response_uaddr = reinterpret_cast<uint64_t>(&resp),
      .error = 0};

    int rc = ioctl(fd, SEV_SNP_GUEST_MSG_REPORT, &payload);
    if (rc < 0)
    {
      LOG_FAIL_FMT("IOCTL call failed: {}", strerror(errno));
      LOG_FAIL_FMT("Payload error: {}", payload.error);
      throw std::logic_error("Failed to issue ioctl SEV_SNP_GUEST_MSG_REPORT");
    }
  }

  const snp::Attestation& Attestation::get() const
  {
    return resp.report;
  }

  std::vector<uint8_t> Attestation::get_raw()
  {
    auto quote_bytes = reinterpret_cast<uint8_t*>(&resp.report);
    return {quote_bytes, quote_bytes + resp.report_size};
  }
}
