/* This file was generated by KreMLin <https://github.com/FStarLang/kremlin>
 * KreMLin invocation: /mnt/e/everest/verify/kremlin/krml -I /mnt/e/everest/verify/hacl-star/code/old/lib/kremlin -I /mnt/e/everest/verify/kremlin/kremlib/compat -I /mnt/e/everest/verify/hacl-star/specs -I /mnt/e/everest/verify/hacl-star/specs/old -I . -fparentheses -fcurly-braces -fno-shadow -ccopt -march=native -verbose -ldopt -flto -tmpdir chacha-c -add-include "kremlib.h" -minimal -bundle Hacl.Chacha20=* -skip-compilation chacha-c/out.krml -o chacha-c/Hacl_Chacha20.c
 * F* version: ebf0a2cc
 * KreMLin version: e9a42a80
 */


#ifndef __Hacl_Chacha20_H
#define __Hacl_Chacha20_H


#include "kremlib.h"

typedef uint8_t *Hacl_Chacha20_uint8_p;

typedef uint32_t Hacl_Chacha20_uint32_t;

void Hacl_Chacha20_chacha20_key_block(uint8_t *block, uint8_t *k, uint8_t *n1, uint32_t ctr);

/*
  This function implements Chacha20

  val chacha20 :
    output: uint8_p ->
    plain: uint8_p{disjoint output plain} ->
    len: uint32_t{v len = length output /\ v len = length plain} ->
    key: uint8_p{length key = 32} ->
    nonce: uint8_p{length nonce = 12} ->
    ctr: uint32_t{v ctr + (length plain / 64) < pow2 32}
  -> Stack unit
      (requires
        (fun h -> live h output /\ live h plain /\ live h nonce /\ live h key))
      (ensures
        (fun h0 _ h1 ->
            live h1 output /\ live h0 plain /\ modifies_1 output h0 h1 /\
            live h0 nonce /\ live h0 key /\
            h1.[ output ] ==
            chacha20_encrypt_bytes h0.[ key ] h0.[ nonce ] (v ctr) h0.[ plain ])
      )
*/
void
Hacl_Chacha20_chacha20(
  uint8_t *output,
  uint8_t *plain,
  uint32_t len,
  uint8_t *k,
  uint8_t *n1,
  uint32_t ctr
);

#define __Hacl_Chacha20_H_DEFINED
#endif
