// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

/**
 * This module polyfills CCF's native functions for use in
 * unit tests that run in Node.js instead of CCF.
 * It must be imported before all other imports like so:
 *
 * ```
 * import '@microsoft/ccf-app/polyfill.js';
 * ```
 *
 * Note that some functionality is not polyfilled,
 * for example historic state (for historical endpoints).
 *
 * @module
 */

import * as crypto from "crypto";
import { TextEncoder, TextDecoder } from "util";

// Note: It is important that only types are imported here to prevent executing
// the module at this point (which would query the ccf global before we polyfilled it).
import {
  CCF,
  KvMaps,
  KvMap,
  JsonCompatible,
  CryptoKeyPair,
  WrapAlgoParams,
  DigestAlgorithm,
} from "./global.js";

// JavaScript's Map uses reference equality for non-primitive types,
// whereas CCF compares the content of the ArrayBuffer.
// To achieve CCF's semantics, all keys are base64-encoded.
class KvMapPolyfill implements KvMap {
  map = new Map<string, ArrayBuffer>();

  has(key: ArrayBuffer): boolean {
    return this.map.has(base64(key));
  }
  get(key: ArrayBuffer): ArrayBuffer | undefined {
    return this.map.get(base64(key));
  }
  set(key: ArrayBuffer, value: ArrayBuffer): KvMap {
    this.map.set(base64(key), value);
    return this;
  }
  delete(key: ArrayBuffer): boolean {
    return this.map.delete(base64(key));
  }
  forEach(
    callback: (value: ArrayBuffer, key: ArrayBuffer, kvmap: KvMap) => void
  ): void {
    this.map.forEach((value, key, _) => {
      callback(value, unbase64(key), this);
    });
  }
}

class CCFPolyfill implements CCF {
  kv = new Proxy(<KvMaps>{}, {
    get: (target, name, receiver) => {
      if (typeof name === "string") {
        return name in target
          ? target[name]
          : (target[name] = new KvMapPolyfill());
      }
      return Reflect.get(target, name, receiver);
    },
  });

  rpc = {
    setApplyWrites(force: boolean) {
      throw new Error("Not implemented");
    },
  };

  strToBuf(s: string): ArrayBuffer {
    return typedArrToArrBuf(new TextEncoder().encode(s));
  }

  bufToStr(v: ArrayBuffer): string {
    return new TextDecoder().decode(v);
  }

  jsonCompatibleToBuf<T extends JsonCompatible<T>>(v: T): ArrayBuffer {
    return this.strToBuf(JSON.stringify(v));
  }

  bufToJsonCompatible<T extends JsonCompatible<T>>(v: ArrayBuffer): T {
    return JSON.parse(this.bufToStr(v));
  }

  generateAesKey(size: number): ArrayBuffer {
    return nodeBufToArrBuf(crypto.randomBytes(size / 8));
  }

  generateRsaKeyPair(size: number, exponent?: number): CryptoKeyPair {
    const rsaKeyPair = crypto.generateKeyPairSync("rsa", {
      modulusLength: size,
      publicExponent: exponent,
      publicKeyEncoding: {
        type: "spki",
        format: "pem",
      },
      privateKeyEncoding: {
        type: "pkcs8",
        format: "pem",
      },
    });
    return rsaKeyPair;
  }

  wrapKey(
    key: ArrayBuffer,
    wrappingKey: ArrayBuffer,
    parameters: WrapAlgoParams
  ): ArrayBuffer {
    if (parameters.name === "RSA-OAEP") {
      return nodeBufToArrBuf(
        crypto.publicEncrypt(
          {
            key: Buffer.from(wrappingKey),
            oaepHash: "sha256",
            oaepLabel: parameters.label
              ? new Uint8Array(parameters.label)
              : undefined,
            padding: crypto.constants.RSA_PKCS1_OAEP_PADDING,
          },
          new Uint8Array(key)
        )
      );
    } else if (parameters.name === "AES-KWP") {
      const iv = Buffer.from("A65959A6", "hex"); // defined in RFC 5649
      const cipher = crypto.createCipheriv(
        "id-aes256-wrap-pad",
        new Uint8Array(wrappingKey),
        iv
      );
      return nodeBufToArrBuf(
        Buffer.concat([cipher.update(new Uint8Array(key)), cipher.final()])
      );
    } else if (parameters.name === "RSA-OAEP-AES-KWP") {
      const randomAesKey = this.generateAesKey(parameters.aesKeySize);
      const wrap1 = this.wrapKey(randomAesKey, wrappingKey, {
        name: "RSA-OAEP",
        label: parameters.label,
      });
      const wrap2 = this.wrapKey(key, randomAesKey, {
        name: "AES-KWP",
      });
      return nodeBufToArrBuf(
        Buffer.concat([Buffer.from(wrap1), Buffer.from(wrap2)])
      );
    } else {
      throw new Error("unsupported wrapAlgo.name");
    }
  }

  digest(algorithm: DigestAlgorithm, data: ArrayBuffer): ArrayBuffer {
    if (algorithm === "SHA-256") {
      return nodeBufToArrBuf(
        crypto.createHash("sha256").update(new Uint8Array(data)).digest()
      );
    } else {
      throw new Error("unsupported algorithm");
    }
  }
}

(<any>globalThis).ccf = new CCFPolyfill();

function nodeBufToArrBuf(buf: Buffer): ArrayBuffer {
  // Note: buf.buffer is not safe, see docs.
  const arrBuf = new ArrayBuffer(buf.byteLength);
  buf.copy(new Uint8Array(arrBuf));
  return arrBuf;
}

function typedArrToArrBuf(ta: ArrayBufferView) {
  return ta.buffer.slice(ta.byteOffset, ta.byteOffset + ta.byteLength);
}

function base64(buf: ArrayBuffer): string {
  return Buffer.from(buf).toString("base64");
}

function unbase64(s: string): ArrayBuffer {
  return nodeBufToArrBuf(Buffer.from(s, "base64"));
}
