/* 
  This file was generated by KreMLin <https://github.com/FStarLang/kremlin>
  KreMLin invocation: /data/everest/everest/kremlin/krml -silent -minimal -bundle EverCrypt+EverCrypt.AutoConfig2+EverCrypt.HKDF+EverCrypt.HMAC+EverCrypt.Hash+EverCrypt.Hash.Incremental+EverCrypt.Cipher+EverCrypt.Poly1305+EverCrypt.Chacha20Poly1305+EverCrypt.Curve25519=*[rename=EverCrypt] -library EverCrypt,EverCrypt.* -add-include <inttypes.h> -add-include <stdbool.h> -add-include <kremlin/internal/types.h> -skip-compilation -tmpdir dist/evercrypt-external-headers/ obj/prims.krml obj/FStar_Pervasives_Native.krml obj/FStar_Pervasives.krml obj/FStar_Float.krml obj/FStar_Mul.krml obj/FStar_Preorder.krml obj/FStar_Calc.krml obj/FStar_Squash.krml obj/FStar_Classical.krml obj/FStar_StrongExcludedMiddle.krml obj/FStar_FunctionalExtensionality.krml obj/FStar_List_Tot_Base.krml obj/FStar_List_Tot_Properties.krml obj/FStar_List_Tot.krml obj/FStar_Seq_Base.krml obj/FStar_Seq_Properties.krml obj/FStar_Seq.krml obj/FStar_Math_Lib.krml obj/FStar_Math_Lemmas.krml obj/FStar_BitVector.krml obj/FStar_UInt.krml obj/FStar_UInt32.krml obj/FStar_UInt64.krml obj/FStar_UInt8.krml obj/FStar_Exn.krml obj/FStar_Set.krml obj/FStar_Monotonic_Witnessed.krml obj/FStar_Ghost.krml obj/FStar_ErasedLogic.krml obj/FStar_PropositionalExtensionality.krml obj/FStar_PredicateExtensionality.krml obj/FStar_TSet.krml obj/FStar_Monotonic_Heap.krml obj/FStar_Heap.krml obj/FStar_ST.krml obj/FStar_All.krml obj/FStar_IO.krml obj/FStar_Map.krml obj/Vale_Lib_Seqs_s.krml obj/Vale_Def_Words_s.krml obj/Vale_Def_Words_Four_s.krml obj/Vale_Def_Words_Two_s.krml obj/Vale_Def_Words_Seq_s.krml obj/Vale_Def_Opaque_s.krml obj/Vale_Def_Types_s.krml obj/Vale_X64_Machine_s.krml obj/Vale_Lib_Map16.krml obj/Vale_Def_Prop_s.krml obj/Vale_X64_Xmms.krml obj/Vale_X64_Regs.krml obj/Vale_X64_Instruction_s.krml obj/Vale_Lib_Meta.krml obj/Vale_Def_Words_Two.krml obj/Vale_Lib_Seqs.krml obj/Vale_Def_TypesNative_s.krml obj/Vale_Arch_TypesNative.krml obj/Vale_Def_Words_Seq.krml obj/Vale_Arch_Types.krml obj/FStar_Option.krml obj/Vale_Lib_Set.krml obj/Vale_X64_Bytes_Code_s.krml obj/Vale_AES_AES_s.krml obj/Vale_Math_Poly2_Defs_s.krml obj/Vale_Math_Poly2_s.krml obj/Vale_Math_Poly2_Bits_s.krml obj/FStar_Monotonic_HyperHeap.krml obj/FStar_Monotonic_HyperStack.krml obj/FStar_HyperStack.krml obj/FStar_HyperStack_ST.krml obj/FStar_HyperStack_All.krml obj/FStar_Kremlin_Endianness.krml obj/FStar_Int.krml obj/FStar_Int64.krml obj/FStar_Int63.krml obj/FStar_Int32.krml obj/FStar_Int16.krml obj/FStar_Int8.krml obj/FStar_UInt63.krml obj/FStar_UInt16.krml obj/FStar_Int_Cast.krml obj/FStar_UInt128.krml obj/Spec_Hash_Definitions.krml obj/Spec_Hash_Lemmas0.krml obj/Spec_Hash_PadFinish.krml obj/Spec_Loops.krml obj/FStar_List.krml obj/Spec_SHA2_Constants.krml obj/Spec_SHA2.krml obj/Vale_X64_CryptoInstructions_s.krml obj/Vale_X64_CPU_Features_s.krml obj/Vale_X64_Instructions_s.krml obj/Vale_X64_Machine_Semantics_s.krml obj/Vale_X64_Bytes_Semantics.krml obj/FStar_Universe.krml obj/FStar_GSet.krml obj/FStar_ModifiesGen.krml obj/FStar_Range.krml obj/FStar_Reflection_Types.krml obj/FStar_Tactics_Types.krml obj/FStar_Tactics_Result.krml obj/FStar_Tactics_Effect.krml obj/FStar_Tactics_Util.krml obj/FStar_Reflection_Data.krml obj/FStar_Reflection_Const.krml obj/FStar_Char.krml obj/FStar_String.krml obj/FStar_Order.krml obj/FStar_Reflection_Basic.krml obj/FStar_Reflection_Derived.krml obj/FStar_Tactics_Builtins.krml obj/FStar_Reflection_Formula.krml obj/FStar_Reflection_Derived_Lemmas.krml obj/FStar_Reflection.krml obj/FStar_Tactics_Derived.krml obj/FStar_Tactics_Logic.krml obj/FStar_Tactics.krml obj/FStar_BigOps.krml obj/LowStar_Monotonic_Buffer.krml obj/LowStar_BufferView_Down.krml obj/LowStar_BufferView_Up.krml obj/Vale_Interop_Views.krml obj/LowStar_Buffer.krml obj/LowStar_Modifies.krml obj/LowStar_ModifiesPat.krml obj/LowStar_BufferView.krml obj/Vale_Lib_BufferViewHelpers.krml obj/LowStar_ImmutableBuffer.krml obj/Vale_Interop_Types.krml obj/Vale_Interop_Base.krml obj/Vale_Interop.krml obj/Vale_X64_Memory.krml obj/Vale_X64_Stack_i.krml obj/Vale_X64_Stack_Sems.krml obj/Vale_X64_BufferViewStore.krml obj/Vale_X64_Memory_Sems.krml obj/Vale_X64_State.krml obj/Vale_X64_StateLemmas.krml obj/Vale_X64_Lemmas.krml obj/Vale_X64_Print_s.krml obj/Vale_X64_Decls.krml obj/Vale_X64_Taint_Semantics.krml obj/Vale_X64_InsLemmas.krml obj/Vale_X64_QuickCode.krml obj/Vale_X64_InsAes.krml obj/Vale_Curve25519_Fast_lemmas_internal.krml obj/Vale_Curve25519_Fast_defs.krml obj/FStar_Tactics_CanonCommSwaps.krml obj/FStar_Algebra_CommMonoid.krml obj/FStar_Tactics_CanonCommMonoid.krml obj/FStar_Tactics_CanonCommSemiring.krml obj/Vale_Curve25519_FastUtil_helpers.krml obj/Vale_Curve25519_FastHybrid_helpers.krml obj/Vale_Curve25519_Fast_lemmas_external.krml obj/Vale_X64_QuickCodes.krml obj/Vale_X64_InsBasic.krml obj/Vale_X64_InsMem.krml obj/Vale_X64_InsVector.krml obj/Vale_X64_InsStack.krml obj/Vale_Curve25519_X64_FastHybrid.krml obj/Vale_Curve25519_FastSqr_helpers.krml obj/Vale_Curve25519_X64_FastSqr.krml obj/Vale_Curve25519_FastMul_helpers.krml obj/Vale_Curve25519_X64_FastMul.krml obj/Vale_Curve25519_X64_FastWide.krml obj/Vale_Curve25519_X64_FastUtil.krml obj/Vale_X64_MemoryAdapters.krml obj/Vale_Interop_Assumptions.krml obj/Vale_Interop_X64.krml obj/Vale_AsLowStar_ValeSig.krml obj/Vale_AsLowStar_LowStarSig.krml obj/Vale_AsLowStar_MemoryHelpers.krml obj/Vale_AsLowStar_Wrapper.krml obj/Vale_Stdcalls_X64_Fadd.krml obj/Vale_Wrapper_X64_Fadd.krml obj/Vale_Math_Poly2_Defs.krml obj/Vale_Math_Poly2.krml obj/Vale_Math_Poly2_Lemmas.krml obj/Vale_Math_Poly2_Bits.krml obj/Vale_Math_Poly2_Words.krml obj/Vale_AES_GF128_s.krml obj/Vale_AES_GF128.krml obj/Vale_AES_OptPublic.krml obj/Vale_AES_X64_GF128_Mul.krml obj/Vale_AES_X64_PolyOps.krml obj/Vale_X64_Stack.krml obj/Vale_AES_GCTR_s.krml obj/Vale_Lib_Workarounds.krml obj/Vale_AES_GCM_helpers.krml obj/Vale_AES_GCTR.krml obj/Vale_AES_AES256_helpers.krml obj/Vale_AES_X64_AES256.krml obj/Vale_AES_AES_helpers.krml obj/Vale_AES_X64_AES128.krml obj/Vale_AES_X64_AES.krml obj/FStar_BV.krml obj/Vale_Lib_Bv_s.krml obj/Vale_Math_Bits.krml obj/Vale_AES_GHash_s.krml obj/Vale_AES_GHash.krml obj/Vale_AES_X64_GF128_Init.krml obj/Vale_Lib_Operator.krml obj/Lib_LoopCombinators.krml obj/FStar_Int_Cast_Full.krml obj/Lib_IntTypes.krml obj/Lib_Sequence.krml obj/Spec_SHA3_Constants.krml obj/Spec_SHA1.krml obj/Spec_MD5.krml obj/Spec_Hash.krml obj/Spec_Hash_Incremental.krml obj/Spec_Hash_Lemmas.krml obj/LowStar_BufferOps.krml obj/C_Loops.krml obj/C_Endianness.krml obj/Hacl_Hash_Lemmas.krml obj/Hacl_Hash_Definitions.krml obj/Hacl_Hash_PadFinish.krml obj/Hacl_Hash_MD.krml obj/Spec_SHA2_Lemmas.krml obj/Vale_SHA_SHA_helpers.krml obj/Vale_X64_InsSha.krml obj/Vale_SHA_X64.krml obj/Vale_Stdcalls_X64_Sha.krml obj/Vale_SHA_Simplify_Sha.krml obj/Vale_Wrapper_X64_Sha.krml obj/Hacl_Hash_Core_SHA2_Constants.krml obj/Hacl_Hash_Core_SHA2.krml obj/Lib_RawIntTypes.krml obj/Lib_Loops.krml obj/Lib_ByteSequence.krml obj/Lib_Buffer.krml obj/FStar_Reflection_Arith.krml obj/FStar_Tactics_BV.krml obj/Vale_Lib_Tactics.krml obj/Vale_Poly1305_Bitvectors.krml obj/Vale_Math_Lemmas_Int.krml obj/FStar_Tactics_Canon.krml obj/Vale_Poly1305_Spec_s.krml obj/Vale_Poly1305_Math.krml obj/Vale_Poly1305_Util.krml obj/Vale_Poly1305_X64.krml obj/Vale_Stdcalls_X64_Poly.krml obj/Vale_Wrapper_X64_Poly.krml obj/FStar_Endianness.krml obj/Vale_Arch_BufferFriend.krml obj/Hacl_Hash_SHA2.krml obj/Hacl_Hash_Core_SHA1.krml obj/Hacl_Hash_SHA1.krml obj/Hacl_Hash_Core_MD5.krml obj/Hacl_Hash_MD5.krml obj/C.krml obj/C_String.krml obj/C_Failure.krml obj/FStar_Int128.krml obj/FStar_Int31.krml obj/FStar_UInt31.krml obj/FStar_Integers.krml obj/EverCrypt_StaticConfig.krml obj/Vale_Lib_X64_Cpuid.krml obj/Vale_Lib_X64_Cpuidstdcall.krml obj/Vale_Stdcalls_X64_Cpuid.krml obj/Vale_Wrapper_X64_Cpuid.krml obj/EverCrypt_TargetConfig.krml obj/EverCrypt_AutoConfig2.krml obj/EverCrypt_Helpers.krml obj/EverCrypt_Hash.krml obj/Hacl_Impl_Curve25519_Lemmas.krml obj/Spec_Curve25519_Lemmas.krml obj/Spec_Curve25519.krml obj/Hacl_Spec_Curve25519_Field51_Definition.krml obj/Hacl_Spec_Curve25519_Field51_Lemmas.krml obj/Hacl_Spec_Curve25519_Field51.krml obj/Hacl_Impl_Curve25519_Field51.krml obj/Hacl_Spec_Curve25519_Finv.krml obj/Hacl_Spec_Curve25519_Field64_Definition.krml obj/Hacl_Spec_Curve25519_Field64_Lemmas.krml obj/Hacl_Spec_Curve25519_Field64_Core.krml obj/Hacl_Spec_Curve25519_Field64.krml obj/Vale_Stdcalls_X64_Fswap.krml obj/Vale_Wrapper_X64_Fswap.krml obj/Vale_X64_Print_Inline_s.krml obj/Vale_Inline_X64_Fswap_inline.krml obj/Vale_Stdcalls_X64_Fsqr.krml obj/Vale_Wrapper_X64_Fsqr.krml obj/Vale_Inline_X64_Fsqr_inline.krml obj/Vale_Stdcalls_X64_Fmul.krml obj/Vale_Wrapper_X64_Fmul.krml obj/Vale_Inline_X64_Fmul_inline.krml obj/Vale_Stdcalls_X64_Fsub.krml obj/Vale_Wrapper_X64_Fsub.krml obj/Vale_Inline_X64_Fadd_inline.krml obj/Hacl_Impl_Curve25519_Field64_Core.krml obj/Hacl_Impl_Curve25519_Field64.krml obj/Hacl_Impl_Curve25519_Fields.krml obj/Lib_ByteBuffer.krml obj/Hacl_Impl_Curve25519_Finv.krml obj/Hacl_Curve25519_Finv_Field51.krml obj/Hacl_Bignum25519.krml obj/Hacl_Impl_Ed25519_PointAdd.krml obj/Hacl_Impl_Ed25519_PointDouble.krml obj/Hacl_Impl_Ed25519_SwapConditional.krml obj/Hacl_Impl_Ed25519_Ladder.krml obj/Hacl_Impl_Ed25519_PointCompress.krml obj/Hacl_Impl_Ed25519_SecretExpand.krml obj/Hacl_Impl_Ed25519_SecretToPublic.krml obj/Hacl_Impl_BignumQ_Mul.krml obj/Hacl_Impl_Load56.krml obj/Hacl_Impl_Store56.krml obj/Hacl_Impl_SHA512_ModQ.krml obj/Hacl_Impl_Ed25519_Sign_Steps.krml obj/Hacl_Impl_Ed25519_Sign.krml obj/Hacl_Impl_Ed25519_Sign_Expanded.krml obj/LowStar_Vector.krml obj/LowStar_Regional.krml obj/LowStar_RVector.krml obj/Vale_AES_GCM_s.krml obj/Vale_AES_GCM.krml obj/Hacl_Spec_Curve25519_AddAndDouble.krml obj/Hacl_Impl_Curve25519_AddAndDouble.krml obj/Lib_IntVector_Intrinsics.krml obj/Lib_IntVector.krml obj/Vale_AES_Gcm_simplify.krml obj/Vale_AES_X64_GHash.krml obj/Vale_AES_X64_AESCTR.krml obj/Vale_AES_X64_AESCTRplain.krml obj/Vale_AES_X64_GCTR.krml obj/Vale_AES_X64_GCMencrypt.krml obj/Vale_AES_X64_GCMdecrypt.krml obj/Vale_Stdcalls_X64_GCMdecrypt.krml obj/Vale_Stdcalls_X64_Aes.krml obj/Vale_Wrapper_X64_AES.krml obj/Vale_Wrapper_X64_GCMdecrypt.krml obj/Spec_Chacha20.krml obj/Hacl_Impl_Chacha20_Core32.krml obj/Hacl_Impl_Chacha20.krml obj/Hacl_Spec_Poly1305_Vec.krml obj/Hacl_Impl_Poly1305_Lemmas.krml obj/Hacl_Spec_Poly1305_Field32xN.krml obj/Hacl_Poly1305_Field32xN_Lemmas.krml obj/Hacl_Spec_Poly1305_Field32xN_Lemmas.krml obj/Hacl_Impl_Poly1305_Field32xN.krml obj/Hacl_Impl_Poly1305_Fields.krml obj/Spec_Poly1305.krml obj/Hacl_Spec_Poly1305_Equiv_Lemmas.krml obj/Hacl_Spec_Poly1305_Equiv.krml obj/Hacl_Impl_Poly1305.krml obj/Hacl_Poly1305_32.krml obj/Spec_Chacha20Poly1305.krml obj/Hacl_Impl_Chacha20Poly1305_PolyCore.krml obj/Hacl_Impl_Chacha20Poly1305_Poly.krml obj/Hacl_Impl_Chacha20Poly1305.krml obj/FStar_Dyn.krml obj/EverCrypt_Vale.krml obj/EverCrypt_Specs.krml obj/EverCrypt_OpenSSL.krml obj/EverCrypt_Hacl.krml obj/EverCrypt_BCrypt.krml obj/EverCrypt_Cipher.krml obj/Hacl_Impl_Curve25519_Generic.krml obj/Hacl_Curve25519_51.krml obj/Hacl_Curve25519_64.krml obj/EverCrypt_Curve25519.krml obj/Hacl_Poly1305_128.krml obj/Hacl_Poly1305_256.krml obj/Vale_Poly1305_Equiv.krml obj/Vale_Poly1305_CallingFromLowStar.krml obj/EverCrypt_Poly1305.krml obj/Spec_HMAC.krml obj/Hacl_HMAC.krml obj/EverCrypt_HMAC.krml obj/Spec_HKDF.krml obj/EverCrypt_HKDF.krml obj/EverCrypt.krml obj/MerkleTree_Spec.krml obj/MerkleTree_New_High.krml obj/LowStar_Regional_Instances.krml obj/MerkleTree_New_Low.krml obj/MerkleTree_New_Low_Serialization.krml obj/Vale_AES_X64_AESopt2.krml obj/Vale_AES_X64_AESGCM.krml obj/Vale_AES_X64_GCMencryptOpt.krml obj/Vale_AES_X64_GCMdecryptOpt.krml obj/Vale_Stdcalls_X64_GCMdecryptOpt.krml obj/Vale_Wrapper_X64_GCMdecryptOpt.krml obj/Vale_Wrapper_X64_GCMdecryptOpt256.krml obj/Vale_Stdcalls_X64_GCMencryptOpt.krml obj/Vale_Wrapper_X64_GCMencryptOpt.krml obj/Vale_Wrapper_X64_GCMencryptOpt256.krml obj/Vale_Stdcalls_X64_AesHash.krml obj/Vale_Wrapper_X64_AEShash.krml obj/Spec_AEAD.krml obj/EverCrypt_Error.krml obj/EverCrypt_AEAD.krml obj/WasmSupport.krml obj/MerkleTree_New_High_Correct_Base.krml obj/MerkleTree_New_High_Correct_Rhs.krml obj/MerkleTree_New_High_Correct_Path.krml obj/MerkleTree_New_High_Correct_Flushing.krml obj/MerkleTree_New_High_Correct_Insertion.krml obj/MerkleTree_New_High_Correct.krml obj/EverCrypt_Hash_Incremental.krml obj/Test_Hash.krml obj/Spec_SHA3.krml obj/Hacl_Impl_SHA3.krml obj/TestLib.krml obj/EverCrypt_Chacha20Poly1305.krml obj/Hacl_SHA3.krml obj/Lib_PrintBuffer.krml obj/Hacl_Test_CSHAKE.krml obj/Spec_Hash_Test.krml obj/Hacl_Impl_Ed25519_Pow2_252m2.krml obj/Hacl_Impl_Ed25519_RecoverX.krml obj/Hacl_Impl_Ed25519_PointDecompress.krml obj/Hacl_Impl_Ed25519_PointEqual.krml obj/Hacl_Impl_Ed25519_Verify.krml obj/Hacl_Ed25519.krml obj/Hacl_Test_Ed25519.krml obj/Vale_Stdcalls_X64_GCTR.krml obj/Vale_X64_Leakage_s.krml obj/Vale_Wrapper_X64_GCTR.krml obj/Test_Vectors_Chacha20Poly1305.krml obj/Lib_RandomBuffer.krml obj/Test_Vectors_Aes128.krml obj/Vale_Test_X64_Args.krml obj/Vale_Stdcalls_X64_GCMencrypt.krml obj/LowStar_Endianness.krml obj/Hacl_Hash_Agile.krml obj/Vale_Test_X64_Vale_memcpy.krml obj/Hacl_Test_SHA3.krml obj/Test_Vectors_Curve25519.krml obj/Vale_Test_X64_Memcpy.krml obj/Spec_Chacha20Poly1305_Test.krml obj/Vale_Lib_Lists.krml obj/Vale_Wrapper_X64_GCMencrypt.krml obj/Vale_AES_X64_AESopt.krml obj/Test_Vectors_Poly1305.krml obj/Vale_AsLowStar_Test.krml obj/Vale_X64_Leakage_Helpers.krml obj/Vale_X64_Leakage_Ins.krml obj/Vale_X64_Leakage.krml obj/Spec_SHA3_Test.krml obj/Test_Lowstarize.krml obj/Test_Vectors.krml obj/Test_NoHeap.krml obj/Test_Vectors_Aes128Gcm.krml obj/Spec_Curve25519_Test.krml obj/Test.krml obj/Spec_Chacha20_Test.krml
  F* version: 7c70b890
  KreMLin version: b511d90c
 */

#include "EverCrypt.h"

typedef struct EverCrypt_aes128_key_s_s
{
  EverCrypt_aes128_key_s_tags tag;
  union {
    FStar_Dyn_dyn case_AES128_OPENSSL;
    FStar_Dyn_dyn case_AES128_BCRYPT;
    struct 
    {
      uint8_t *w;
      uint8_t *sbox;
    }
    case_AES128_VALE;
    struct 
    {
      uint8_t *w;
      uint8_t *sbox;
    }
    case_AES128_HACL;
  }
  ;
}
EverCrypt_aes128_key_s;

typedef struct EverCrypt_aes256_key_s_s
{
  EverCrypt_aes256_key_s_tags tag;
  union {
    FStar_Dyn_dyn case_AES256_OPENSSL;
    FStar_Dyn_dyn case_AES256_BCRYPT;
    struct 
    {
      uint8_t *w;
      uint8_t *sbox;
    }
    case_AES256_HACL;
  }
  ;
}
EverCrypt_aes256_key_s;

typedef struct EverCrypt__aead_state_s
{
  EverCrypt__aead_state_tags tag;
  union {
    FStar_Dyn_dyn case_AEAD_OPENSSL;
    FStar_Dyn_dyn case_AEAD_BCRYPT;
    uint8_t *case_AEAD_AES128_GCM_VALE;
    uint8_t *case_AEAD_AES256_GCM_VALE;
    uint8_t *case_AEAD_CHACHA20_POLY1305_HACL;
  }
  ;
}
EverCrypt__aead_state;

typedef struct EverCrypt__dh_state_s
{
  EverCrypt__dh_state_tags tag;
  FStar_Dyn_dyn st;
}
EverCrypt__dh_state;

typedef struct EverCrypt__ecdh_state_s
{
  EverCrypt__ecdh_state_tags tag;
  FStar_Dyn_dyn st;
}
EverCrypt__ecdh_state;

void
EverCrypt_aes128_gcm_encrypt(
  uint8_t *key,
  uint8_t *iv,
  uint8_t *ad,
  uint32_t adlen,
  uint8_t *plaintext,
  uint32_t len1,
  uint8_t *cipher1,
  uint8_t *tag
)
{
  bool ite;
  if (EverCrypt_AutoConfig2_wants_vale())
  {
    ite = EverCrypt_AutoConfig2_has_aesni();
  }
  else
  {
    ite = false;
  }
  if (ite)
  {
    uint8_t expanded[176U] = { 0U };
    old_aes128_key_expansion(key, expanded);
    uint8_t iv_[16U] = { 0U };
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t plaintext_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(plaintext_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof plaintext_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t cipher_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(cipher_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof cipher_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t ad_[(adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(ad_, 0U, (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof ad_[0U]);
    memcpy(iv_, iv, (uint32_t)12U * sizeof iv[0U]);
    memcpy(plaintext_, plaintext, len1 * sizeof plaintext[0U]);
    memcpy(ad_, ad, adlen * sizeof ad[0U]);
    gcm_args
    b =
      {
        .plain = plaintext_, .plain_len = (uint64_t)len1, .aad = ad_, .aad_len = (uint64_t)adlen,
        .iv = iv_, .expanded_key = expanded, .cipher = cipher_, .tag = tag
      };
    old_gcm128_encrypt(&b);
    memcpy(cipher1, cipher_, len1 * sizeof cipher_[0U]);
  }
  else
  {
    KRML_EXIT;
  }
}

void
EverCrypt_aes256_gcm_encrypt(
  uint8_t *key,
  uint8_t *iv,
  uint8_t *ad,
  uint32_t adlen,
  uint8_t *plaintext,
  uint32_t len1,
  uint8_t *cipher1,
  uint8_t *tag
)
{
  bool ite;
  if (EverCrypt_AutoConfig2_wants_vale())
  {
    ite = EverCrypt_AutoConfig2_has_aesni();
  }
  else
  {
    ite = false;
  }
  if (ite)
  {
    uint8_t expanded[240U] = { 0U };
    old_aes256_key_expansion(key, expanded);
    uint8_t iv_[16U] = { 0U };
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t plaintext_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(plaintext_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof plaintext_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t cipher_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(cipher_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof cipher_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t ad_[(adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(ad_, 0U, (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof ad_[0U]);
    memcpy(iv_, iv, (uint32_t)12U * sizeof iv[0U]);
    memcpy(plaintext_, plaintext, len1 * sizeof plaintext[0U]);
    memcpy(ad_, ad, adlen * sizeof ad[0U]);
    gcm_args
    b =
      {
        .plain = plaintext_, .plain_len = (uint64_t)len1, .aad = ad_, .aad_len = (uint64_t)adlen,
        .iv = iv_, .expanded_key = expanded, .cipher = cipher_, .tag = tag
      };
    old_gcm256_encrypt(&b);
    memcpy(cipher1, cipher_, len1 * sizeof cipher_[0U]);
  }
  else
  {
    KRML_EXIT;
  }
}

uint32_t
EverCrypt_aes256_gcm_decrypt(
  uint8_t *key,
  uint8_t *iv,
  uint8_t *ad,
  uint32_t adlen,
  uint8_t *plaintext,
  uint32_t len1,
  uint8_t *cipher1,
  uint8_t *tag
)
{
  bool ite;
  if (EverCrypt_AutoConfig2_wants_vale())
  {
    ite = EverCrypt_AutoConfig2_has_aesni();
  }
  else
  {
    ite = false;
  }
  if (ite)
  {
    uint8_t expanded[240U] = { 0U };
    old_aes256_key_expansion(key, expanded);
    uint8_t iv_[16U] = { 0U };
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t plaintext_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(plaintext_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof plaintext_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t cipher_[(len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(cipher_,
      0U,
      (len1 + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof cipher_[0U]);
    KRML_CHECK_SIZE(sizeof (uint8_t), (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U);
    uint8_t ad_[(adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U];
    memset(ad_, 0U, (adlen + (uint32_t)15U) / (uint32_t)16U * (uint32_t)16U * sizeof ad_[0U]);
    memcpy(iv_, iv, (uint32_t)12U * sizeof iv[0U]);
    memcpy(cipher_, cipher1, len1 * sizeof cipher1[0U]);
    memcpy(ad_, ad, adlen * sizeof ad[0U]);
    gcm_args
    b =
      {
        .plain = cipher_, .plain_len = (uint64_t)len1, .aad = ad_, .aad_len = (uint64_t)adlen,
        .iv = iv_, .expanded_key = expanded, .cipher = plaintext_, .tag = tag
      };
    uint32_t ret = old_gcm256_decrypt(&b);
    memcpy(plaintext, plaintext_, len1 * sizeof plaintext_[0U]);
    uint32_t r;
    if (ret == (uint32_t)0U)
    {
      r = (uint32_t)1U;
    }
    else
    {
      r = (uint32_t)0U;
    }
    return r;
  }
  else
  {
    KRML_EXIT;
    return 0;
  }
}