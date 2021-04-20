// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "verifier.h"

#include "crypto/openssl/openssl_wrappers.h"
#include "public_key.h"
#include "rsa_key_pair.h"

#include <openssl/evp.h>
#include <openssl/x509.h>

namespace crypto
{
  using namespace OpenSSL;

  MDType Verifier_OpenSSL::get_md_type(int mdt) const
  {
    switch (mdt)
    {
      case NID_undef:
        return MDType::NONE;
      case NID_sha1:
        return MDType::SHA1;
      case NID_sha256:
        return MDType::SHA256;
      case NID_sha384:
        return MDType::SHA384;
      case NID_sha512:
        return MDType::SHA512;
      default:
        return MDType::NONE;
    }
    return MDType::NONE;
  }

  Verifier_OpenSSL::Verifier_OpenSSL(const std::vector<uint8_t>& c) : Verifier()
  {
    Unique_BIO certbio(c);
    if (!(cert = Unique_X509(certbio, true)))
    {
      BIO_reset(certbio);
      if (!(cert = Unique_X509(certbio, false)))
      {
        throw std::invalid_argument(fmt::format(
          "OpenSSL error: {}", OpenSSL::error_string(ERR_get_error())));
      }
    }

    int mdnid, pknid, secbits;
    X509_get_signature_info(cert, &mdnid, &pknid, &secbits, 0);
    md_type = get_md_type(mdnid);

    EVP_PKEY* pk = X509_get_pubkey(cert);

    if (EVP_PKEY_get0_EC_KEY(pk))
    {
      public_key = std::make_unique<PublicKey_OpenSSL>(pk);
    }
    else if (EVP_PKEY_get0_RSA(pk))
    {
      public_key = std::make_unique<RSAPublicKey_OpenSSL>(pk);
    }
    else
    {
      throw std::logic_error("unsupported public key type");
    }
  }

  Verifier_OpenSSL::~Verifier_OpenSSL() {}

  std::vector<uint8_t> Verifier_OpenSSL::cert_der()
  {
    Unique_BIO mem;
    CHECK1(i2d_X509_bio(mem, cert));

    BUF_MEM* bptr;
    BIO_get_mem_ptr(mem, &bptr);
    return {(uint8_t*)bptr->data, (uint8_t*)bptr->data + bptr->length};
  }

  Pem Verifier_OpenSSL::cert_pem()
  {
    Unique_BIO mem;
    CHECK1(PEM_write_bio_X509(mem, cert));

    BUF_MEM* bptr;
    BIO_get_mem_ptr(mem, &bptr);
    return Pem((uint8_t*)bptr->data, bptr->length);
  }

  bool Verifier_OpenSSL::verify_certificate(
    const std::vector<const Pem*>& trusted_certs)
  {
    Unique_X509_STORE store;
    Unique_X509_STORE_CTX store_ctx;

    for (auto& pem : trusted_certs)
    {
      Unique_BIO tcbio(*pem);
      Unique_X509 tc(tcbio, true);
      CHECK1(X509_STORE_add_cert(store, tc));
    }

    CHECK1(X509_STORE_CTX_init(store_ctx, store, cert, NULL));
    return X509_verify_cert(store_ctx) == 1;
  }

  bool Verifier_OpenSSL::is_self_signed() const
  {
    return X509_get_extension_flags(cert) & EXFLAG_SS;
  }

  std::string Verifier_OpenSSL::serial_number() const
  {
    const ASN1_INTEGER* sn = X509_get0_serialNumber(cert);
    Unique_BIO mem;
    i2a_ASN1_INTEGER(mem, sn);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(mem, &bptr);
    return std::string(bptr->data, bptr->length);
  }
}
