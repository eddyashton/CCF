// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/pal/attestation_sev_snp.h"

#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

// Based on the SEV-SNP ABI Spec document at
// https://www.amd.com/system/files/TechDocs/56860.pdf

/* linux kernel 5.15.* versions of the ioctls that talk to the PSP */

namespace ccf::pal::snp::ioctl5
{
  constexpr auto DEVICE = "/dev/sev";

  struct GuestRequest
  {
    uint8_t req_msg_type;
    uint8_t rsp_msg_type;
    uint8_t msg_version;
    uint16_t request_len;
    uint64_t request_uaddr;
    uint16_t response_len;
    uint64_t response_uaddr;
    uint32_t error; /* firmware error code on failure (see psp-sev.h) */
  };

  // Table 102
  enum MsgType
  {
    MSG_TYPE_INVALID = 0,
    MSG_CPUID_REQ,
    MSG_CPUID_RSP,
    MSG_KEY_REQ,
    MSG_KEY_RSP,
    MSG_REPORT_REQ,
    MSG_REPORT_RSP,
    MSG_EXPORT_REQ,
    MSG_EXPORT_RSP,
    MSG_IMPORT_REQ,
    MSG_IMPORT_RSP,
    MSG_ABSORB_REQ,
    MSG_ABSORB_RSP,
    MSG_VMRK_REQ,
    MSG_VMRK_RSP,
    MSG_TYPE_MAX
  };

  // Table 22
#pragma pack(push, 1)
  struct AttestationReq
  {
    uint8_t report_data[snp_attestation_report_data_size];
    uint32_t vmpl = 0;
    uint8_t reserved[28];
  };
#pragma pack(pop)

  // Table 25
#pragma pack(push, 1)
  struct AttestationResp
  {
    uint32_t status;
    uint32_t report_size;
    uint8_t reserved[0x20 - 0x8];
    struct Attestation report;
    uint8_t padding[64];
    // padding to the size of SEV_SNP_REPORT_RSP_BUF_SZ (i.e., 1280 bytes)
  };
#pragma pack(pop)

  constexpr char SEV_GUEST_IOC_TYPE = 'S';
  constexpr int SEV_SNP_GUEST_MSG_REPORT =
    _IOWR(SEV_GUEST_IOC_TYPE, 0x1, struct snp::ioctl5::GuestRequest);

  static inline bool is_sev_snp()
  {
    return access(DEVICE, W_OK) == 0;
  }

  class Attestation : public AttestationInterface
  {
    AttestationReq req = {};
    AttestationResp resp = {};

  public:
    Attestation(const PlatformAttestationReportData& report_data);

    const snp::Attestation& get() const override;
    std::vector<uint8_t> get_raw() override;
  };
}
