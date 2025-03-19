// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ccf/pal/snp_ioctl6.h"

#include "ccf/ds/logger.h"

namespace ccf::pal::snp::ioctl6
{
  Attestation::Attestation(const PlatformAttestationReportData& report_data)
  {
    AttestationReq req = {};
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
      throw std::logic_error(
        fmt::format("Failed to open \"{}\" ({})", DEVICE, fd));
    }

    // Documented at
    // https://www.kernel.org/doc/html/latest/virt/coco/sev-guest.html
    GuestRequestAttestation payload = {
      .req_data = &req, .resp_wrapper = &padded_resp, .exit_info = {0}};

    int rc = ioctl(fd, SEV_SNP_GUEST_MSG_REPORT, &payload);
    if (rc < 0)
    {
      LOG_FAIL_FMT("IOCTL call failed: {}", strerror(errno));
      LOG_FAIL_FMT(
        "Exit info, fw_error: {} vmm_error: {}",
        payload.exit_info.errors.fw,
        payload.exit_info.errors.vmm);
      throw std::logic_error("Failed to issue ioctl SEV_SNP_GUEST_MSG_REPORT");
    }

    if (!safety_padding_intact(padded_resp))
    {
      // This occurs if a kernel/firmware upgrade causes the response to
      // overflow the struct so it is better to fail early than deal with
      // memory corruption.
      throw std::logic_error("IOCTL overwrote safety padding.");
    }
  }

  const snp::Attestation& Attestation::get() const
  {
    return padded_resp.data.report;
  }

  std::vector<uint8_t> Attestation::get_raw()
  {
    auto quote_bytes = reinterpret_cast<uint8_t*>(&padded_resp.data.report);
    return {quote_bytes, quote_bytes + padded_resp.data.report_size};
  }

  DerivedKey::DerivedKey()
  {
    int fd = open(DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0)
    {
      throw std::logic_error(
        fmt::format("Failed to open \"{}\" ({})", DEVICE, fd));
    }

    // This req by default mixes in HostData and the CPU VCEK
    DerivedKeyReq req = {};
    // We must also mix in the measurement
    req.guest_field_select.measurement = 1;
    GuestRequestDerivedKey payload = {
      .req_data = &req, .resp_wrapper = &padded_resp, .exit_info = {0}};
    int rc = ioctl(fd, SEV_SNP_GUEST_MSG_DERIVED_KEY, &payload);
    if (rc < 0)
    {
      LOG_FAIL_FMT("IOCTL call failed: {}", strerror(errno));
      LOG_FAIL_FMT(
        "Exit info, fw_error: {} vmm_error: {}",
        payload.exit_info.errors.fw,
        payload.exit_info.errors.vmm);
      throw std::logic_error(
        "Failed to issue ioctl SEV_SNP_GUEST_MSG_DERIVED_KEY");
    }

    if (!safety_padding_intact(padded_resp))
    {
      // This occurs if a kernel/firmware upgrade causes the response to
      // overflow the struct so it is better to fail early than deal with
      // memory corruption.
      throw std::logic_error("IOCTL overwrote safety padding.");
    }

    if (padded_resp.data.status != 0)
    {
      LOG_FAIL_FMT("SNP_GUEST_DERIVED_KEY failed: {}", padded_resp.data.status);
      throw std::logic_error(
        "Failed to issue ioctl SEV_SNP_GUEST_MSG_DERIVED_KEY");
    }
  }

  DerivedKey::~DerivedKey()
  {
    OPENSSL_cleanse(padded_resp.data.data, sizeof(padded_resp.data.data));
  }

  std::span<const uint8_t> DerivedKey::get_raw()
  {
    return std::span<const uint8_t>{padded_resp.data.data};
  }
}