/* This file was generated by KreMLin <https://github.com/FStarLang/kremlin>
 * KreMLin invocation: /mnt/e/everest/verify/kremlin/krml -library Lib.PrintBuffer -library Lib.RandomBuffer -bundle Lib.*[rename=Hacl_Lib] -bundle Hacl.Test.* -bundle Hacl.Hash.MD5+Hacl.Hash.Core.MD5+Hacl.Hash.SHA1+Hacl.Hash.Core.SHA1+Hacl.Hash.SHA2+Hacl.Hash.Core.SHA2+Hacl.Hash.Core.SHA2.Constants=Hacl.Hash.*[rename=Hacl_Hash] -bundle Hacl.Impl.SHA3+Hacl.SHA3=[rename=Hacl_SHA3] -bundle Prims -bundle LowStar.* -bundle C,C.String,C.Loops,Spec.Loops,C.Endianness,FStar.*[rename=Hacl_Kremlib] -bundle Test.*,WindowsHack -minimal -add-include "kremlin/internal/types.h" -add-include "kremlin/internal/target.h" -add-include "kremlin/c_endianness.h" -add-include <string.h> -fno-shadow -fcurly-braces -tmpdir dist/compact/ -skip-compilation -bundle Spec.*[rename=Hacl_Spec] .extracted/prims.krml .extracted/FStar_Pervasives_Native.krml .extracted/FStar_Pervasives.krml .extracted/FStar_Mul.krml .extracted/FStar_Squash.krml .extracted/FStar_Classical.krml .extracted/FStar_StrongExcludedMiddle.krml .extracted/FStar_FunctionalExtensionality.krml .extracted/FStar_List_Tot_Base.krml .extracted/FStar_List_Tot_Properties.krml .extracted/FStar_List_Tot.krml .extracted/FStar_Seq_Base.krml .extracted/FStar_Seq_Properties.krml .extracted/FStar_Seq.krml .extracted/FStar_Math_Lib.krml .extracted/FStar_Math_Lemmas.krml .extracted/FStar_BitVector.krml .extracted/FStar_UInt.krml .extracted/FStar_UInt32.krml .extracted/FStar_Int.krml .extracted/FStar_Int16.krml .extracted/FStar_Preorder.krml .extracted/FStar_Ghost.krml .extracted/FStar_ErasedLogic.krml .extracted/FStar_Set.krml .extracted/FStar_PropositionalExtensionality.krml .extracted/FStar_PredicateExtensionality.krml .extracted/FStar_TSet.krml .extracted/FStar_Monotonic_Heap.krml .extracted/FStar_Heap.krml .extracted/FStar_Map.krml .extracted/FStar_Monotonic_HyperHeap.krml .extracted/FStar_Monotonic_HyperStack.krml .extracted/FStar_HyperStack.krml .extracted/FStar_Monotonic_Witnessed.krml .extracted/FStar_HyperStack_ST.krml .extracted/FStar_HyperStack_All.krml .extracted/FStar_UInt64.krml .extracted/FStar_Exn.krml .extracted/FStar_ST.krml .extracted/FStar_All.krml .extracted/Lib_LoopCombinators.krml .extracted/FStar_Int64.krml .extracted/FStar_Int63.krml .extracted/FStar_Int32.krml .extracted/FStar_Int8.krml .extracted/FStar_UInt63.krml .extracted/FStar_UInt16.krml .extracted/FStar_UInt8.krml .extracted/FStar_Int_Cast.krml .extracted/FStar_UInt128.krml .extracted/FStar_Int_Cast_Full.krml .extracted/Lib_IntTypes.krml .extracted/Lib_Sequence.krml .extracted/Spec_SHA3_Constants.krml .extracted/Lib_RawIntTypes.krml .extracted/FStar_Kremlin_Endianness.krml .extracted/Spec_Hash_Definitions.krml .extracted/Spec_Hash_Lemmas0.krml .extracted/Spec_Hash_PadFinish.krml .extracted/Spec_Loops.krml .extracted/Spec_SHA1.krml .extracted/Spec_MD5.krml .extracted/FStar_List.krml .extracted/Spec_SHA2_Constants.krml .extracted/Spec_SHA2.krml .extracted/Spec_Hash.krml .extracted/FStar_Order.krml .extracted/Spec_Hash_Incremental.krml .extracted/Spec_Hash_Lemmas.krml .extracted/FStar_Universe.krml .extracted/FStar_GSet.krml .extracted/FStar_ModifiesGen.krml .extracted/FStar_Range.krml .extracted/FStar_Reflection_Types.krml .extracted/FStar_Tactics_Types.krml .extracted/FStar_Tactics_Result.krml .extracted/FStar_Tactics_Effect.krml .extracted/FStar_Tactics_Util.krml .extracted/FStar_Reflection_Data.krml .extracted/FStar_Reflection_Const.krml .extracted/FStar_Char.krml .extracted/FStar_String.krml .extracted/FStar_Reflection_Basic.krml .extracted/FStar_Reflection_Derived.krml .extracted/FStar_Tactics_Builtins.krml .extracted/FStar_Reflection_Formula.krml .extracted/FStar_Reflection_Derived_Lemmas.krml .extracted/FStar_Reflection.krml .extracted/FStar_Tactics_Derived.krml .extracted/FStar_Tactics_Logic.krml .extracted/FStar_Tactics.krml .extracted/FStar_BigOps.krml .extracted/LowStar_Monotonic_Buffer.krml .extracted/LowStar_Buffer.krml .extracted/LowStar_BufferOps.krml .extracted/C_Loops.krml .extracted/C_Endianness.krml .extracted/Hacl_Hash_Lemmas.krml .extracted/LowStar_Modifies.krml .extracted/Hacl_Hash_Definitions.krml .extracted/Hacl_Hash_PadFinish.krml .extracted/Hacl_Hash_MD.krml .extracted/LowStar_Modifies_Linear.krml .extracted/LowStar_ImmutableBuffer.krml .extracted/Hacl_Hash_Core_SHA1.krml .extracted/Hacl_Hash_SHA1.krml .extracted/Hacl_Hash_Core_MD5.krml .extracted/Hacl_Hash_MD5.krml .extracted/Lib_Loops.krml .extracted/Lib_ByteSequence.krml .extracted/Lib_Buffer.krml .extracted/C.krml .extracted/Lib_ByteBuffer.krml .extracted/C_String.krml .extracted/Spec_SHA3.krml .extracted/Hacl_Impl_SHA3.krml .extracted/Hacl_SHA3.krml .extracted/Lib_PrintBuffer.krml .extracted/Hacl_Test_CSHAKE.krml .extracted/Spec_Hash_Test.krml .extracted/FStar_Float.krml .extracted/FStar_IO.krml .extracted/Spec_SHA3_Test.krml .extracted/Hacl_Hash_Core_SHA2_Constants.krml .extracted/Hacl_Hash_Core_SHA2.krml .extracted/Hacl_Hash_SHA2.krml .extracted/Lib_RandomBuffer.krml .extracted/Hacl_Hash_Agile.krml .extracted/Hacl_Test_SHA3.krml -ccopt -Wno-unused -warn-error @4 -fparentheses Hacl_AES.c Hacl_Curve25519.c Hacl_Ed25519.c Hacl_Chacha20.c AEAD_Poly1305_64.c Hacl_Poly1305_64.c Hacl_Chacha20Poly1305.c Lib_RandomBuffer.c Lib_PrintBuffer.c -o libhacl.a
 * F* version: ebf0a2cc
 * KreMLin version: e9a42a80
 */


#ifndef __Hacl_SHA3_H
#define __Hacl_SHA3_H

#include "Hacl_Kremlib.h"
#include "kremlin/internal/types.h"
#include "kremlin/internal/target.h"
#include "kremlin/c_endianness.h"
#include <string.h>

extern uint32_t Hacl_Impl_SHA3_keccak_rotc[24U];

extern uint32_t Hacl_Impl_SHA3_keccak_piln[24U];

extern uint64_t Hacl_Impl_SHA3_keccak_rndc[24U];

uint64_t Hacl_Impl_SHA3_rotl(uint64_t a, uint32_t b);

void Hacl_Impl_SHA3_state_permute(uint64_t *s);

void Hacl_Impl_SHA3_loadState(uint32_t rateInBytes, uint8_t *input, uint64_t *s);

void Hacl_Impl_SHA3_storeState(uint32_t rateInBytes, uint64_t *s, uint8_t *res);

void
Hacl_Impl_SHA3_absorb(
  uint64_t *s,
  uint32_t rateInBytes,
  uint32_t inputByteLen,
  uint8_t *input,
  uint8_t delimitedSuffix
);

void
Hacl_Impl_SHA3_squeeze(
  uint64_t *s,
  uint32_t rateInBytes,
  uint32_t outputByteLen,
  uint8_t *output
);

void
Hacl_Impl_SHA3_keccak(
  uint32_t rate,
  uint32_t capacity,
  uint32_t inputByteLen,
  uint8_t *input,
  uint8_t delimitedSuffix,
  uint32_t outputByteLen,
  uint8_t *output
);

void
Hacl_SHA3_shake128_hacl(
  uint32_t inputByteLen,
  uint8_t *input,
  uint32_t outputByteLen,
  uint8_t *output
);

void
Hacl_SHA3_shake256_hacl(
  uint32_t inputByteLen,
  uint8_t *input,
  uint32_t outputByteLen,
  uint8_t *output
);

void Hacl_SHA3_sha3_224(uint32_t inputByteLen, uint8_t *input, uint8_t *output);

void Hacl_SHA3_sha3_256(uint32_t inputByteLen, uint8_t *input, uint8_t *output);

void Hacl_SHA3_sha3_384(uint32_t inputByteLen, uint8_t *input, uint8_t *output);

void Hacl_SHA3_sha3_512(uint32_t inputByteLen, uint8_t *input, uint8_t *output);

#define __Hacl_SHA3_H_DEFINED
#endif
