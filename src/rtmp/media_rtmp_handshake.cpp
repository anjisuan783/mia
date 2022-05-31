//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
// This file is borrowed from srs with some modifications.

#include "rtmp/media_rtmp_handshake.h"

#include <openssl/hmac.h>
#include <openssl/dh.h>
#include <openssl/evp.h>

#include "common/media_define.h"
#include "common/media_log.h"
#include "utils/media_msg_chain.h"
#include "utils/media_protocol_utility.h"
#include "utils/media_kernel_buffer.h"
#include "connection/h/media_io.h"

// For randomly generate the handshake bytes.
#define RTMP_SIG_HANDSHAKE RTMP_SIG_KEY "(" RTMP_SIG_VERSION ")"

// @see https://wiki.openssl.org/index.php/OpenSSL_1.1.0_Changes
#if OPENSSL_VERSION_NUMBER < 0x10100000L

HMAC_CTX *HMAC_CTX_new(void) {
  HMAC_CTX *ctx = (HMAC_CTX *)malloc(sizeof(*ctx));
  if (ctx != NULL) {
    HMAC_CTX_init(ctx);
  }
  return ctx;
}

void HMAC_CTX_free(HMAC_CTX *ctx) {
  if (ctx != NULL) {
    HMAC_CTX_cleanup(ctx);
    free(ctx);
  }
}

static void DH_get0_key(const DH *dh, const BIGNUM **pub_key,
    const BIGNUM **priv_key) {
  if (pub_key != NULL) {
    *pub_key = dh->pub_key;
  }
  if (priv_key != NULL) {
    *priv_key = dh->priv_key;
  }
}

static int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g) {
  /* If the fields p and g in d are NULL, the corresponding input
    * parameters MUST be non-NULL.  q may remain NULL.
    */
  if ((dh->p == NULL && p == NULL) || (dh->g == NULL && g == NULL))
    return 0;
  
  if (p != NULL) {
    BN_free(dh->p);
    dh->p = p;
  }
  if (q != NULL) {
    BN_free(dh->q);
    dh->q = q;
  }
  if (g != NULL) {
    BN_free(dh->g);
    dh->g = g;
  }
  
  if (q != NULL) {
    dh->length = BN_num_bits(q);
  }
  
  return 1;
}

static int DH_set_length(DH *dh, long length) {
  dh->length = length;
  return 1;
}

#endif

namespace ma {

static log4cxx::LoggerPtr logger = log4cxx::Logger::getLogger("ma.rtmp");

namespace {
const int HANDSHAKE_REQUEST_SIZE = 1537;  // [1+1536]
const int HANDSHAKE_RESPONSE_SIZE = 3073; // [1+1536+1536]
const int HANDSHAKE_ACK_SIZE = 1536;   // [1536]

// The digest key generate size.
#define SRS_OpensslHashSize 512
// 68bytes FMS key which is used to sign the sever packet.
uint8_t SrsGenuineFMSKey[] = {
  0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
  0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
  0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
  0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
  0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
  0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
  0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
  0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
  0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
}; // 68

// 62bytes FP key which is used to sign the client packet.
uint8_t SrsGenuineFPKey[] = {
  0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
  0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
  0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
  0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
  0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
  0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
  0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
  0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
}; // 62

srs_error_t do_openssl_HMACsha256(HMAC_CTX* ctx, const void* data, 
  int data_size, void* digest, unsigned int* digest_size) {
  srs_error_t err = srs_success;
  
  if (HMAC_Update(ctx, (unsigned char *) data, data_size) < 0) {
    return srs_error_new(ERROR_OpenSslSha256Update, "hmac update");
  }
  
  if (HMAC_Final(ctx, (unsigned char *) digest, digest_size) < 0) {
    return srs_error_new(ERROR_OpenSslSha256Final, "hmac final");
  }
  
  return err;
}

/**
 * sha256 digest algorithm.
 * @param key the sha256 key, NULL to use EVP_Digest, for instance,
 *       hashlib.sha256(data).digest().
 */
srs_error_t openssl_HMACsha256(const void* key, int key_size, 
    const void* data, int data_size, void* digest) {
  srs_error_t err = srs_success;
  
  unsigned int digest_size = 0;
  
  unsigned char* temp_key = (unsigned char*)key;
  unsigned char* temp_digest = (unsigned char*)digest;
  
  if (key == NULL) {
    // use data to digest.
    // @see ./crypto/sha/sha256t.c
    // @see ./crypto/evp/digest.c
    if (EVP_Digest(data, data_size, temp_digest, &digest_size, 
        EVP_sha256(), NULL) < 0) {
      return srs_error_new(ERROR_OpenSslSha256EvpDigest, "evp digest");
    }
  } else {
    // use key-data to digest.
    HMAC_CTX *ctx = HMAC_CTX_new();
    if (ctx == NULL) {
      return srs_error_new(ERROR_OpenSslCreateHMAC, "hmac new");
    }
    // @remark, if no key, use EVP_Digest to digest,
    // for instance, in python, hashlib.sha256(data).digest().
    if (HMAC_Init_ex(ctx, temp_key, key_size, EVP_sha256(), NULL) < 0) {
      HMAC_CTX_free(ctx);
      return srs_error_new(ERROR_OpenSslSha256Init, "hmac init");
    }
    
    err=do_openssl_HMACsha256(ctx, data, data_size, temp_digest, &digest_size);
    HMAC_CTX_free(ctx);
    
    if (err != srs_success) {
      return srs_error_wrap(err, "hmac sha256");
    }
  }
  
  if (digest_size != 32) {
    return srs_error_new(ERROR_OpenSslSha256DigestSize, 
        "digest size %d", digest_size);
  }
  
  return err;
}

#define RFC2409_PRIME_1024 \
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
    "29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
    "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
    "FFFFFFFFFFFFFFFF"


  // The DH wrapper.
class SrsDH final {
 public:
  SrsDH();
  ~SrsDH();

  void close();

  // Initialize dh, generate the public and private key.
  // @param ensure_128bytes_public_key whether ensure public key is 128bytes,
  //       sometimes openssl generate 127bytes public key.
  //       default to false to donot ensure.
  srs_error_t initialize(bool ensure_128bytes_public_key = false);
  // Copy the public key.
  // @param pkey the bytes to copy the public key.
  // @param pkey_size the max public key size, output the actual public key size.
  //       user should never ignore this size.
  // @remark, when ensure_128bytes_public_key, the size always 128.
  srs_error_t copy_public_key(char* pkey, int32_t& pkey_size);
  // Generate and copy the shared key.
  // Generate the shared key with peer public key.
  // @param ppkey peer public key.
  // @param ppkey_size the size of ppkey.
  // @param skey the computed shared key.
  // @param skey_size the max shared key size, output the actual shared key size.
  //       user should never ignore this size.
  srs_error_t copy_shared_key(const char* ppkey, int32_t ppkey_size, 
      char* skey, int32_t& skey_size);
 private:
  srs_error_t do_initialize();

 private:
  DH* pdh;
};

// The schema type.
enum srs_schema_type {
  srs_schema_invalid = 2,
  
  // The key-digest sequence
  srs_schema0 = 0,
  
  // The digest-key sequence
  // @remark, FMS requires the schema1(digest-key), or connect failed.
  //
  srs_schema1 = 1,
};

// The 764bytes key structure
//     random-data: (offset)bytes
//     key-data: 128bytes
//     random-data: (764-offset-128-4)bytes
//     offset: 4bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class key_block final {
 public:
  // (offset)bytes
  char* random0;
  int random0_size;
  
  // 128bytes
  char key[128];
  
  // (764-offset-128-4)bytes
  char* random1;
  int random1_size;
  
  // 4bytes
  int32_t offset;
 public:
  key_block();
  ~key_block();
  // Parse key block from c1s1.
  // if created, user must free it by srs_key_block_free
  // @stream contains c1s1_key_bytes the key start bytes
  srs_error_t parse(SrsBuffer* stream);
 private:
  // Calculate the offset of key,
  // The key->offset cannot be used as the offset of key.
  int calc_valid_offset();
};

// The 764bytes digest structure
//     offset: 4bytes
//     random-data: (offset)bytes
//     digest-data: 32bytes
//     random-data: (764-4-offset-32)bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class digest_block {
public:
  // 4bytes
  int32_t offset;
  
  // (offset)bytes
  char* random0;
  int random0_size;
  
  // 32bytes
  char digest[32];
  
  // (764-4-offset-32)bytes
  char* random1;
  int random1_size;
public:
  digest_block();
  virtual ~digest_block();
public:
  // Parse digest block from c1s1.
  // if created, user must free it by srs_digest_block_free
  // @stream contains c1s1_digest_bytes the digest start bytes
  srs_error_t parse(SrsBuffer* stream);
private:
  // Calculate the offset of digest,
  // The key->offset cannot be used as the offset of digest.
  int calc_valid_offset();
};

class c1s1;

// The c1s1 strategy, use schema0 or schema1.
// The template method class to defines common behaviors,
// while the concrete class to implements in schema0 or schema1.
class c1s1_strategy {
protected:
  key_block key;
  digest_block digest;
public:
  c1s1_strategy();
  virtual ~c1s1_strategy();
public:
  // Get the scema.
  virtual srs_schema_type schema() = 0;
  // Get the digest.
  char* get_digest();
  // Get the key.
  char* get_key();
  // Copy to bytes.
  // @param size must be 1536.
  srs_error_t dump(c1s1* owner, char* _c1s1, int size);
  // For server: parse the c1s1, discovery the key and digest by schema.
  // use the c1_validate_digest() to valid the digest of c1.
  virtual srs_error_t parse(char* _c1s1, int size) = 0;
public:
  // For client: create and sign c1 by schema.
  // sign the c1, generate the digest.
  //         calc_c1_digest(c1, schema) {
  //            get c1s1-joined from c1 by specified schema
  //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
  //            return digest-data;
  //        }
  //        random fill 1536bytes c1 // also fill the c1-128bytes-key
  //        time = time() // c1[0-3]
  //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
  //        schema = choose schema0 or schema1
  //        digest-data = calc_c1_digest(c1, schema)
  //        copy digest-data to c1
  srs_error_t c1_create(c1s1* owner);
  // For server:  validate the parsed c1 schema
  srs_error_t c1_validate_digest(c1s1* owner, bool& is_valid);
  // For server:  create and sign the s1 from c1.
  //       // decode c1 try schema0 then schema1
  //       c1-digest-data = get-c1-digest-data(schema0)
  //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
  //           c1-key-data = get-c1-key-data(schema0)
  //           schema = schema0
  //       } else {
  //           c1-digest-data = get-c1-digest-data(schema1)
  //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
  //               switch to simple handshake.
  //               return
  //           }
  //           c1-key-data = get-c1-key-data(schema1)
  //           schema = schema1
  //       }
  //
  //       // Generate s1
  //       random fill 1536bytes s1
  //       time = time() // c1[0-3]
  //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
  //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
  //       get c1s1-joined by specified schema
  //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
  //       copy s1-digest-data and s1-key-data to s1.
  // @param c1, to get the peer_pub_key of client.
  srs_error_t s1_create(c1s1* owner, c1s1* c1);
  // For server:  validate the parsed s1 schema
  srs_error_t s1_validate_digest(c1s1* owner, bool& is_valid);
public:
  // Calculate the digest for c1
  srs_error_t calc_c1_digest(c1s1* owner, char* c1_digest);
  // Calculate the digest for s1
  srs_error_t calc_s1_digest(c1s1* owner, char* s1_digest);
  // Copy whole c1s1 to bytes.
  // @param size must always be 1536 with digest, and 1504 without digest.
  virtual srs_error_t copy_to(c1s1* owner, char* bytes, 
      int size, bool with_digest) = 0;
  // Copy time and version to stream.
  void copy_time_version(SrsBuffer* stream, c1s1* owner);
  // Copy key to stream.
  void copy_key(SrsBuffer* stream);
  // Copy digest to stream.
  void copy_digest(SrsBuffer* stream, bool with_digest);
};

// The c1s1 schema0
//     key: 764bytes
//     digest: 764bytes
class c1s1_strategy_schema0 : public c1s1_strategy {
public:
  c1s1_strategy_schema0();
  virtual ~c1s1_strategy_schema0();
  srs_schema_type schema() override;
  srs_error_t parse(char* _c1s1, int size) override;
  srs_error_t copy_to(c1s1* owner, char* bytes, 
      int size, bool with_digest) override;
};

// The c1s1 schema1
//     digest: 764bytes
//     key: 764bytes
class c1s1_strategy_schema1 : public c1s1_strategy {
public:
  c1s1_strategy_schema1();
  ~c1s1_strategy_schema1();
  srs_schema_type schema() override;
  srs_error_t parse(char* _c1s1, int size) override;
  srs_error_t copy_to(c1s1* owner, char* bytes, 
      int size, bool with_digest) override;
};

// The c1s1 schema0
//     time: 4bytes
//     version: 4bytes
//     key: 764bytes
//     digest: 764bytes
// The c1s1 schema1
//     time: 4bytes
//     version: 4bytes
//     digest: 764bytes
//     key: 764bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class c1s1 {
 public:
  c1s1();
  ~c1s1();

  // Get the scema.
  srs_schema_type schema();
  // Get the digest key.
  char* get_digest();
  // Get the key.
  char* get_key();

  // Copy to bytes.
  // @param size, must always be 1536.
  srs_error_t dump(char* _c1s1, int size);
  // For server:  parse the c1s1, discovery the key and digest by schema.
  // @param size, must always be 1536.
  // use the c1_validate_digest() to valid the digest of c1.
  // use the s1_validate_digest() to valid the digest of s1.
  srs_error_t parse(char* _c1s1, int size, srs_schema_type _schema);

  // For client:  create and sign c1 by schema.
  // sign the c1, generate the digest.
  //         calc_c1_digest(c1, schema) {
  //            get c1s1-joined from c1 by specified schema
  //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
  //            return digest-data;
  //        }
  //        random fill 1536bytes c1 // also fill the c1-128bytes-key
  //        time = time() // c1[0-3]
  //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
  //        schema = choose schema0 or schema1
  //        digest-data = calc_c1_digest(c1, schema)
  //        copy digest-data to c1
  srs_error_t c1_create(srs_schema_type _schema);
  // For server:  validate the parsed c1 schema
  srs_error_t c1_validate_digest(bool& is_valid);

  // For server:  create and sign the s1 from c1.
  //       // decode c1 try schema0 then schema1
  //       c1-digest-data = get-c1-digest-data(schema0)
  //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
  //           c1-key-data = get-c1-key-data(schema0)
  //           schema = schema0
  //       } else {
  //           c1-digest-data = get-c1-digest-data(schema1)
  //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
  //               switch to simple handshake.
  //               return
  //           }
  //           c1-key-data = get-c1-key-data(schema1)
  //           schema = schema1
  //       }
  //
  //       // Generate s1
  //       random fill 1536bytes s1
  //       time = time() // c1[0-3]
  //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
  //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
  //       get c1s1-joined by specified schema
  //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
  //       copy s1-digest-data and s1-key-data to s1.
  srs_error_t s1_create(c1s1* c1);
  // For server:  validate the parsed s1 schema
  srs_error_t s1_validate_digest(bool& is_valid);

 public:
  // 4bytes
  int32_t time;
  // 4bytes
  int32_t version;
  // 764bytes+764bytes
  c1s1_strategy* payload;
};

// The c2s2 complex handshake structure.
// random-data: 1504bytes
// digest-data: 32bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class c2s2 {
public:
  char random[1504];
  char digest[32];
public:
  c2s2();
  ~c2s2();
public:
  // Copy to bytes.
  // @param size, must always be 1536.
  srs_error_t dump(char* _c2s2, int size);
  // Parse the c2s2
  // @param size, must always be 1536.
  srs_error_t parse(char* _c2s2, int size);
public:
  // Create c2.
  // random fill c2s2 1536 bytes
  //
  // // client generate C2, or server valid C2
  // temp-key = HMACsha256(s1-digest, FPKey, 62)
  // c2-digest-data = HMACsha256(c2-random-data, temp-key, 32)
  srs_error_t c2_create(c1s1* s1);
  
  // Validate the c2 from client.
  srs_error_t c2_validate(c1s1* s1, bool& is_valid);
public:
  // Create s2.
  // random fill c2s2 1536 bytes
  //
  // For server generate S2, or client valid S2
  // temp-key = HMACsha256(c1-digest, FMSKey, 68)
  // s2-digest-data = HMACsha256(s2-random-data, temp-key, 32)
  srs_error_t s2_create(c1s1* c1);
  
  // Validate the s2 from server.
  srs_error_t s2_validate(c1s1* c1, bool& is_valid);
};

////////////////////////////////////////////////////////////////////////////////
//SrsDH
////////////////////////////////////////////////////////////////////////////////
SrsDH::SrsDH() {
  pdh = NULL;
}

SrsDH::~SrsDH() {
  close();
}

void SrsDH::close() {
  if (pdh != NULL) {
    DH_free(pdh);
    pdh = NULL;
  }
}

srs_error_t SrsDH::initialize(bool ensure_128bytes_public_key) {
  srs_error_t err = srs_success;
  
  for (;;) {
    if ((err = do_initialize()) != srs_success) {
      return srs_error_wrap(err, "init");
    }
    
    if (ensure_128bytes_public_key) {
      const BIGNUM *pub_key = NULL;
      DH_get0_key(pdh, &pub_key, NULL);
      int32_t key_size = BN_num_bytes(pub_key);
      if (key_size != 128) {
          MLOG_CWARN("regenerate 128B key, current=%dB", key_size);
          continue;
      }
    }
    
    break;
  }
  
  return err;
}

srs_error_t SrsDH::copy_public_key(char* pkey, int32_t& pkey_size) {
  srs_error_t err = srs_success;
  
  // copy public key to bytes.
  // sometimes, the key_size is 127, seems ok.
  const BIGNUM *pub_key = NULL;
  DH_get0_key(pdh, &pub_key, NULL);
  int32_t key_size = BN_num_bytes(pub_key);
  srs_assert(key_size > 0);
  
  // maybe the key_size is 127, but dh will write all 128bytes pkey,
  // so, donot need to set/initialize the pkey.
  // @see https://github.com/ossrs/srs/issues/165
  key_size = BN_bn2bin(pub_key, (unsigned char*)pkey);
  srs_assert(key_size > 0);
  
  // output the size of public key.
  // @see https://github.com/ossrs/srs/issues/165
  srs_assert(key_size <= pkey_size);
  pkey_size = key_size;
  
  return err;
}

srs_error_t SrsDH::copy_shared_key(const char* ppkey, 
    int32_t ppkey_size, char* skey, int32_t& skey_size) {
  srs_error_t err = srs_success;
  
  BIGNUM* ppk = NULL;
  if ((ppk = BN_bin2bn((const unsigned char*)ppkey, ppkey_size, 0)) == NULL) {
      return srs_error_new(ERROR_OpenSslGetPeerPublicKey, "bin2bn");
  }
  
  // if failed, donot return, do cleanup, @see ./test/dhtest.c:168
  // maybe the key_size is 127, but dh will write all 128bytes skey,
  // so, donot need to set/initialize the skey.
  // @see https://github.com/ossrs/srs/issues/165
  int32_t key_size = DH_compute_key((unsigned char*)skey, ppk, pdh);
  
  if (key_size < ppkey_size) {
      MLOG_CWARN("shared key size=%d, ppk_size=%d", key_size, ppkey_size);
  }
  
  if (key_size < 0 || key_size > skey_size) {
      err = srs_error_new(ERROR_OpenSslComputeSharedKey, "key size %d", key_size);
  } else {
      skey_size = key_size;
  }
  
  if (ppk) {
      BN_free(ppk);
  }
  
  return err;
}

srs_error_t SrsDH::do_initialize() {
  srs_error_t err = srs_success;
  
  int32_t bits_count = 1024;
  
  close();
  
  //1. Create the DH
  if ((pdh = DH_new()) == NULL) {
      return srs_error_new(ERROR_OpenSslCreateDH, "dh new");
  }
  
  //2. Create his internal p and g
  BIGNUM *p, *g;
  if ((p = BN_new()) == NULL) {
      return srs_error_new(ERROR_OpenSslCreateP, "dh new");
  }
  if ((g = BN_new()) == NULL) {
      BN_free(p);
      return srs_error_new(ERROR_OpenSslCreateG, "bn new");
  }
  DH_set0_pqg(pdh, p, NULL, g);
  
  //3. initialize p and g, @see ./test/ectest.c:260
  if (!BN_hex2bn(&p, RFC2409_PRIME_1024)) {
      return srs_error_new(ERROR_OpenSslParseP1024, "hex2bn");
  }
  // @see ./test/bntest.c:1764
  if (!BN_set_word(g, 2)) {
      return srs_error_new(ERROR_OpenSslSetG, "set word");
  }
  
  // 4. Set the key length
  DH_set_length(pdh, bits_count);
  
  // 5. Generate private and public key
  // @see ./test/dhtest.c:152
  if (!DH_generate_key(pdh)) {
      return srs_error_new(ERROR_OpenSslGenerateDHKeys, "dh generate key");
  }
  
  return err;
}

////////////////////////////////////////////////////////////////////////////////
//key_block
////////////////////////////////////////////////////////////////////////////////
key_block::key_block() {
  offset = (int32_t)srs_random();
  random0 = NULL;
  random1 = NULL;
  
  int valid_offset = calc_valid_offset();
  srs_assert(valid_offset >= 0);
  
  random0_size = valid_offset;
  if (random0_size > 0) {
    random0 = new char[random0_size];
    srs_random_generate(random0, random0_size);
    snprintf(random0, random0_size, "%s", RTMP_SIG_HANDSHAKE);
  }
  
  srs_random_generate(key, sizeof(key));
  
  random1_size = 764 - valid_offset - 128 - 4;
  if (random1_size > 0) {
    random1 = new char[random1_size];
    srs_random_generate(random1, random1_size);
    snprintf(random1, random1_size, "%s", RTMP_SIG_HANDSHAKE);
  }
}

key_block::~key_block() {
  srs_freepa(random0);
  srs_freepa(random1);
}

srs_error_t key_block::parse(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  // the key must be 764 bytes.
  srs_assert(stream->require(764));
  
  // read the last offset first, 760-763
  stream->skip(764 - sizeof(int32_t));
  offset = stream->read_4bytes();
  
  // reset stream to read others.
  stream->skip(-764);
  
  int valid_offset = calc_valid_offset();
  srs_assert(valid_offset >= 0);
  
  random0_size = valid_offset;
  if (random0_size > 0) {
      srs_freepa(random0);
      random0 = new char[random0_size];
      stream->read_bytes(random0, random0_size);
  }
  
  stream->read_bytes(key, 128);
  
  random1_size = 764 - valid_offset - 128 - 4;
  if (random1_size > 0) {
      srs_freepa(random1);
      random1 = new char[random1_size];
      stream->read_bytes(random1, random1_size);
  }
  
  return err;
}

int key_block::calc_valid_offset() {
  int max_offset_size = 764 - 128 - 4;
  
  int valid_offset = 0;
  uint8_t* pp = (uint8_t*)&offset;
  valid_offset += *pp++;
  valid_offset += *pp++;
  valid_offset += *pp++;
  valid_offset += *pp++;
  
  return valid_offset % max_offset_size;
}

digest_block::digest_block() {
  offset = (int32_t)srs_random();
  random0 = NULL;
  random1 = NULL;
  
  int valid_offset = calc_valid_offset();
  srs_assert(valid_offset >= 0);
  
  random0_size = valid_offset;
  if (random0_size > 0) {
      random0 = new char[random0_size];
      srs_random_generate(random0, random0_size);
      snprintf(random0, random0_size, "%s", RTMP_SIG_HANDSHAKE);
  }
  
  srs_random_generate(digest, sizeof(digest));
  
  random1_size = 764 - 4 - valid_offset - 32;
  if (random1_size > 0) {
      random1 = new char[random1_size];
      srs_random_generate(random1, random1_size);
      snprintf(random1, random1_size, "%s", RTMP_SIG_HANDSHAKE);
  }
}

digest_block::~digest_block() {
  srs_freepa(random0);
  srs_freepa(random1);
}

srs_error_t digest_block::parse(SrsBuffer* stream) {
  srs_error_t err = srs_success;
  
  // the digest must be 764 bytes.
  srs_assert(stream->require(764));
  
  offset = stream->read_4bytes();
  
  int valid_offset = calc_valid_offset();
  srs_assert(valid_offset >= 0);
  
  random0_size = valid_offset;
  if (random0_size > 0) {
      srs_freepa(random0);
      random0 = new char[random0_size];
      stream->read_bytes(random0, random0_size);
  }
  
  stream->read_bytes(digest, 32);
  
  random1_size = 764 - 4 - valid_offset - 32;
  if (random1_size > 0) {
      srs_freepa(random1);
      random1 = new char[random1_size];
      stream->read_bytes(random1, random1_size);
  }
  
  return err;
}

int digest_block::calc_valid_offset() {
  int max_offset_size = 764 - 32 - 4;
  
  int valid_offset = 0;
  uint8_t* pp = (uint8_t*)&offset;
  valid_offset += *pp++;
  valid_offset += *pp++;
  valid_offset += *pp++;
  valid_offset += *pp++;
  
  return valid_offset % max_offset_size;
}

c1s1_strategy::c1s1_strategy() { }

c1s1_strategy::~c1s1_strategy() { }

char* c1s1_strategy::get_digest() {
  return digest.digest;
}

char* c1s1_strategy::get_key() {
  return key.key;
}

srs_error_t c1s1_strategy::dump(c1s1* owner, char* _c1s1, int size) {
  srs_assert(size == 1536);
  return copy_to(owner, _c1s1, size, true);
}

srs_error_t c1s1_strategy::c1_create(c1s1* owner) {
  srs_error_t err = srs_success;
  
  // generate digest
  char c1_digest[SRS_OpensslHashSize];
  if ((err = calc_c1_digest(owner, c1_digest)) != srs_success) {
    return srs_error_wrap(err, "sign c1");
  }
  
  memcpy(digest.digest, c1_digest, 32);
  
  return err;
}

srs_error_t c1s1_strategy::c1_validate_digest(c1s1* owner, bool& is_valid) {
  srs_error_t err = srs_success;
  
  char c1_digest[SRS_OpensslHashSize];
  if ((err = calc_c1_digest(owner, c1_digest)) != srs_success) {
    return srs_error_wrap(err, "validate c1");
  }
  
  is_valid = srs_bytes_equals(digest.digest, c1_digest, 32);
  
  return err;
}

srs_error_t c1s1_strategy::s1_create(c1s1* owner, c1s1* c1) {
  srs_error_t err = srs_success;
  
  SrsDH dh;
  
  // ensure generate 128bytes public key.
  if ((err = dh.initialize(true)) != srs_success) {
      return srs_error_wrap(err, "dh init");
  }
  
  // directly generate the public key.
  int pkey_size = 128;
  if ((err = dh.copy_shared_key(c1->get_key(), 128, key.key, pkey_size)) != srs_success) {
      return srs_error_wrap(err, "copy shared key");
  }
  
  // although the public key is always 128bytes, but the share key maybe not.
  // we just ignore the actual key size, but if need to use the key, must use the actual size.
  // TODO: FIXME: use the actual key size.
  //srs_assert(pkey_size == 128);
  
  char s1_digest[SRS_OpensslHashSize];
  if ((err = calc_s1_digest(owner, s1_digest))  != srs_success) {
    return srs_error_wrap(err, "calc s1 digest");
  }
  
  memcpy(digest.digest, s1_digest, 32);
  
  return err;
}

srs_error_t c1s1_strategy::s1_validate_digest(c1s1* owner, bool& is_valid) {
  srs_error_t err = srs_success;
  
  char s1_digest[SRS_OpensslHashSize];
  if ((err = calc_s1_digest(owner, s1_digest)) != srs_success) {
    return srs_error_wrap(err, "validate s1");
  }
   
  is_valid = srs_bytes_equals(digest.digest, s1_digest, 32);
  
  return err;
}

srs_error_t c1s1_strategy::calc_c1_digest(c1s1* owner, char* c1_digest) {
  srs_error_t err = srs_success;
  
  /**
   * c1s1 is splited by digest:
   *     c1s1-part1: n bytes (time, version, key and digest-part1).
   *     digest-data: 32bytes
   *     c1s1-part2: (1536-n-32)bytes (digest-part2)
   * @return a new allocated bytes, user must free it.
   */
  char c1s1_joined_bytes[1536 -32];

  if ((err = copy_to(
      owner, c1s1_joined_bytes, 1536 - 32, false)) != srs_success) {
    return srs_error_wrap(err, "copy bytes");
  }
  
  if ((err = openssl_HMACsha256(SrsGenuineFPKey, 30, c1s1_joined_bytes, 
      1536 - 32, c1_digest)) != srs_success) {
    return srs_error_wrap(err, "calc c1 digest");
  }
  
  return err;
}

srs_error_t c1s1_strategy::calc_s1_digest(c1s1* owner, char* s1_digest) {
  srs_error_t err = srs_success;
  
  /**
   * c1s1 is splited by digest:
   *     c1s1-part1: n bytes (time, version, key and digest-part1).
   *     digest-data: 32bytes
   *     c1s1-part2: (1536-n-32)bytes (digest-part2)
   * @return a new allocated bytes, user must free it.
   */
  char c1s1_joined_bytes[1536 -32];
  if ((err = copy_to(owner, c1s1_joined_bytes, 1536 - 32, false)) != 
      srs_success) {
    return srs_error_wrap(err, "copy bytes");
  }
  
  if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 36, c1s1_joined_bytes, 
      1536 - 32, s1_digest)) != srs_success) {
    return srs_error_wrap(err, "calc s1 digest");
  }
  
  return err;
}

void c1s1_strategy::copy_time_version(SrsBuffer* stream, c1s1* owner) {
  srs_assert(stream->require(8));
  
  // 4bytes time
  stream->write_4bytes(owner->time);
  
  // 4bytes version
  stream->write_4bytes(owner->version);
}

void c1s1_strategy::copy_key(SrsBuffer* stream) {
  srs_assert(key.random0_size >= 0);
  srs_assert(key.random1_size >= 0);
  
  int total = key.random0_size + 128 + key.random1_size + 4;
  srs_assert(stream->require(total));
  
  // 764bytes key block
  if (key.random0_size > 0) {
      stream->write_bytes(key.random0, key.random0_size);
  }
  
  stream->write_bytes(key.key, 128);
  
  if (key.random1_size > 0) {
      stream->write_bytes(key.random1, key.random1_size);
  }
  
  stream->write_4bytes(key.offset);
}

void c1s1_strategy::copy_digest(SrsBuffer* stream, bool with_digest) {
  srs_assert(key.random0_size >= 0);
  srs_assert(key.random1_size >= 0);
  
  int total = 4 + digest.random0_size + digest.random1_size;
  if (with_digest) {
      total += 32;
  }
  srs_assert(stream->require(total));
  
  // 732bytes digest block without the 32bytes digest-data
  // nbytes digest block part1
  stream->write_4bytes(digest.offset);
  
  // digest random padding.
  if (digest.random0_size > 0) {
      stream->write_bytes(digest.random0, digest.random0_size);
  }
  
  // digest
  if (with_digest) {
      stream->write_bytes(digest.digest, 32);
  }
  
  // nbytes digest block part2
  if (digest.random1_size > 0) {
      stream->write_bytes(digest.random1, digest.random1_size);
  }
}

c1s1_strategy_schema0::c1s1_strategy_schema0() { }

c1s1_strategy_schema0::~c1s1_strategy_schema0() { }

srs_schema_type c1s1_strategy_schema0::schema() {
  return srs_schema0;
}

srs_error_t c1s1_strategy_schema0::parse(char* _c1s1, int size) {
  srs_error_t err = srs_success;
  
  srs_assert(size == 1536);
  
  if (true) {
      SrsBuffer stream(_c1s1 + 8, 764);
      
      if ((err = key.parse(&stream)) != srs_success) {
          return srs_error_wrap(err, "parse the c1 key");
      }
  }
  
  if (true) {
      SrsBuffer stream(_c1s1 + 8 + 764, 764);
  
      if ((err = digest.parse(&stream)) != srs_success) {
          return srs_error_wrap(err, "parse the c1 digest");
      }
  }
  
  return err;
}

srs_error_t c1s1_strategy_schema0::copy_to(c1s1* owner, 
    char* bytes, int size, bool with_digest) {
  srs_error_t err = srs_success;
  
  if (with_digest) {
      srs_assert(size == 1536);
  } else {
      srs_assert(size == 1504);
  }
  
  SrsBuffer stream(bytes, size);
  
  copy_time_version(&stream, owner);
  copy_key(&stream);
  copy_digest(&stream, with_digest);
  
  srs_assert(stream.empty());
  
  return err;
}

c1s1_strategy_schema1::c1s1_strategy_schema1() { }

c1s1_strategy_schema1::~c1s1_strategy_schema1() { }

srs_schema_type c1s1_strategy_schema1::schema() {
  return srs_schema1;
}

srs_error_t c1s1_strategy_schema1::parse(char* _c1s1, int size) {
  srs_error_t err = srs_success;
  
  srs_assert(size == 1536);
  
  if (true) {
      SrsBuffer stream(_c1s1 + 8, 764);
      
      if ((err = digest.parse(&stream)) != srs_success) {
          return srs_error_wrap(err, "parse c1 digest");
      }
  }
  
  if (true) {
      SrsBuffer stream(_c1s1 + 8 + 764, 764);
      
      if ((err = key.parse(&stream)) != srs_success) {
          return srs_error_wrap(err, "parse c1 key");
      }
  }
  
  return err;
}

srs_error_t c1s1_strategy_schema1::copy_to(c1s1* owner, 
    char* bytes, int size, bool with_digest) {
  srs_error_t err = srs_success;
  
  if (with_digest) {
      srs_assert(size == 1536);
  } else {
      srs_assert(size == 1504);
  }
  
  SrsBuffer stream(bytes, size);
  
  copy_time_version(&stream, owner);
  copy_digest(&stream, with_digest);
  copy_key(&stream);
  
  srs_assert(stream.empty());
  
  return err;
}

c1s1::c1s1() {
  payload = NULL;
  version = 0;
  time = 0;
}

c1s1::~c1s1() {
  srs_freep(payload);
}

srs_schema_type c1s1::schema() {
  srs_assert(payload != NULL);
  return payload->schema();
}

char* c1s1::get_digest() {
  srs_assert(payload != NULL);
  return payload->get_digest();
}

char* c1s1::get_key() {
  srs_assert(payload != NULL);
  return payload->get_key();
}

srs_error_t c1s1::dump(char* _c1s1, int size) {
  srs_assert(size == 1536);
  srs_assert(payload != NULL);
  return payload->dump(this, _c1s1, size);
}

srs_error_t c1s1::parse(char* _c1s1, int size, srs_schema_type schema) {
  srs_assert(size == 1536);
  
  if (schema != srs_schema0 && schema != srs_schema1) {
      return srs_error_new(ERROR_RTMP_CH_SCHEMA, "parse c1 failed. invalid schema=%d", schema);
  }
  
  SrsBuffer stream(_c1s1, size);
  
  time = stream.read_4bytes();
  version = stream.read_4bytes(); // client c1 version
  
  srs_freep(payload);
  if (schema == srs_schema0) {
      payload = new c1s1_strategy_schema0();
  } else {
      payload = new c1s1_strategy_schema1();
  }
  
  return payload->parse(_c1s1, size);
}

srs_error_t c1s1::c1_create(srs_schema_type schema) {
  if (schema != srs_schema0 && schema != srs_schema1) {
      return srs_error_new(ERROR_RTMP_CH_SCHEMA, "create c1 failed. invalid schema=%d", schema);
  }
  
  // client c1 time and version
  time = (int32_t)::time(NULL);
  version = 0x80000702; // client c1 version
  
  // generate signature by schema
  srs_freep(payload);
  if (schema == srs_schema0) {
      payload = new c1s1_strategy_schema0();
  } else {
      payload = new c1s1_strategy_schema1();
  }
  
  return payload->c1_create(this);
}

srs_error_t c1s1::c1_validate_digest(bool& is_valid) {
  is_valid = false;
  srs_assert(payload);
  return payload->c1_validate_digest(this, is_valid);
}

srs_error_t c1s1::s1_create(c1s1* c1) {
  if (c1->schema() != srs_schema0 && c1->schema() != srs_schema1) {
      return srs_error_new(ERROR_RTMP_CH_SCHEMA, "create s1 failed. invalid schema=%d", c1->schema());
  }
  
  time = ::time(NULL);
  version = 0x01000504; // server s1 version
  
  srs_freep(payload);
  if (c1->schema() == srs_schema0) {
      payload = new c1s1_strategy_schema0();
  } else {
      payload = new c1s1_strategy_schema1();
  }
  
  return payload->s1_create(this, c1);
}

srs_error_t c1s1::s1_validate_digest(bool& is_valid) {
  is_valid = false;
  srs_assert(payload);
  return payload->s1_validate_digest(this, is_valid);
}

c2s2::c2s2() {
  srs_random_generate(random, 1504);
  
  int size = snprintf(random, 1504, "%s", RTMP_SIG_HANDSHAKE);
  srs_assert(size < 1504);
  snprintf(random + 1504 - size, size, "%s", RTMP_SIG_HANDSHAKE);
  
  srs_random_generate(digest, 32);
}

c2s2::~c2s2() { }

srs_error_t c2s2::dump(char* _c2s2, int size) {
  srs_assert(size == 1536);
  
  memcpy(_c2s2, random, 1504);
  memcpy(_c2s2 + 1504, digest, 32);
  
  return srs_success;
}

srs_error_t c2s2::parse(char* _c2s2, int size) {
  srs_assert(size == 1536);
  
  memcpy(random, _c2s2, 1504);
  memcpy(digest, _c2s2 + 1504, 32);
  
  return srs_success;
}

srs_error_t c2s2::c2_create(c1s1* s1) {
  srs_error_t err = srs_success;
  
  char temp_key[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != srs_success) {
      return srs_error_wrap(err, "create c2 temp key");
  }
  
  char _digest[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
      return srs_error_wrap(err, "create c2 digest");
  }
  
  memcpy(digest, _digest, 32);
  
  return err;
}

srs_error_t c2s2::c2_validate(c1s1* s1, bool& is_valid) {
  is_valid = false;
  srs_error_t err = srs_success;
  
  char temp_key[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != srs_success) {
      return srs_error_wrap(err, "create c2 temp key");
  }
  
  char _digest[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
      return srs_error_wrap(err, "create c2 digest");
  }
  
  is_valid = srs_bytes_equals(digest, _digest, 32);
  
  return err;
}

srs_error_t c2s2::s2_create(c1s1* c1) {
  srs_error_t err = srs_success;
  
  char temp_key[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != srs_success) {
      return srs_error_wrap(err, "create s2 temp key");
  }
  
  char _digest[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
      return srs_error_wrap(err, "create s2 digest");
  }
  
  memcpy(digest, _digest, 32);
  
  return err;
}

srs_error_t c2s2::s2_validate(c1s1* c1, bool& is_valid) {
  is_valid = false;
  srs_error_t err = srs_success;
  
  char temp_key[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != srs_success) {
      return srs_error_wrap(err, "create s2 temp key");
  }
  
  char _digest[SRS_OpensslHashSize];
  if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
      return srs_error_wrap(err, "create s2 digest");
  }
  
  is_valid = srs_bytes_equals(digest, _digest, 32);
  
  return err;
}
} //namespace

// store the handshake bytes,
// For smart switch between complex and simple handshake.
class HandshakeHelper final {
 public:
  srs_error_t Handlec0c1(MessageChain*);
  srs_error_t Handles0s1s2(MessageChain*);
  srs_error_t Handlec2(MessageChain*);
  srs_error_t Createc0c1();
  srs_error_t Creates0s1s2(const char* c1 = NULL);
  srs_error_t Createc2();

 public:
  // For RTMP proxy, the real IP.
  uint32_t proxy_real_ip = 0;
  
  char c0c1[HANDSHAKE_REQUEST_SIZE];
  char s0s1s2[HANDSHAKE_RESPONSE_SIZE];
  char c2[HANDSHAKE_ACK_SIZE];
};

// Simple handshake.
// user can try complex handshake first,
// rollback to simple handshake if error ERROR_RTMP_TRY_SIMPLE_HS
class SimpleRtmpHandshake final : public RtmpHandshakeStrategy {
 public:
  // Simple handshake.
  srs_error_t ServerHandshakeWithClient(HandshakeHelper* helper,
      MessageChain* msg, RtmpBufferIO*) override;
  srs_error_t OnClientAck(HandshakeHelper*, MessageChain* msg) override;

  srs_error_t ClientHandshakeWithServer(HandshakeHelper* helper,
      RtmpBufferIO*) override;
  srs_error_t OnServerAck(HandshakeHelper*, MessageChain*, 
      RtmpBufferIO*) override;
};

// Complex handshake,
// @see also crtmp(crtmpserver) or librtmp,
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class ComplexRtmpHandshake final : public RtmpHandshakeStrategy {
 public:
// Complex hanshake.
// @return user must:
//     continue connect app if success,
//     try simple handshake if error is ERROR_RTMP_TRY_SIMPLE_HS,
//     otherwise, disconnect
  srs_error_t ServerHandshakeWithClient(HandshakeHelper*, 
      MessageChain*, RtmpBufferIO*) override;
  srs_error_t OnClientAck(HandshakeHelper*, MessageChain* msg) override;

  srs_error_t ClientHandshakeWithServer(
      HandshakeHelper* helper, RtmpBufferIO*) override;
  srs_error_t OnServerAck(HandshakeHelper*, 
      MessageChain*, RtmpBufferIO*) override;
 private:
  c1s1 c1;
};

////////////////////////////////////////////////////////////////////////////////
//HandshakeHelper
////////////////////////////////////////////////////////////////////////////////
srs_error_t HandshakeHelper::Handlec0c1(MessageChain* msg) {
  uint32_t nsize;

  if (MessageChain::error_ok != msg->Read(
      c0c1, HANDSHAKE_REQUEST_SIZE, &nsize) || 
      HANDSHAKE_REQUEST_SIZE != nsize) {
    return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, 
        "read c0c1, size=%d", nsize);
  }

  // Whether RTMP proxy, @see https://github.com/ossrs/go-oryx/wiki/RtmpProxy
  if (uint8_t(c0c1[0]) == 0xF3) {
    uint16_t nn = uint16_t(c0c1[1])<<8 | uint16_t(c0c1[2]);
    ssize_t nn_consumed = 3 + nn;
    if (nn > 1024) {
      return srs_error_new(ERROR_RTMP_PROXY_EXCEED, 
          "proxy exceed max size, nn=%d", nn);
    }

    // 4B client real IP.
    if (nn >= 4) {
      proxy_real_ip = uint32_t(c0c1[3])<<24 | 
          uint32_t(c0c1[4])<<16 | 
          uint32_t(c0c1[5])<<8 | 
          uint32_t(c0c1[6]);
      nn -= 4;
    }

    memmove(c0c1, c0c1 + nn_consumed, HANDSHAKE_REQUEST_SIZE - nn_consumed);

    int ret = msg->Read(c0c1 + HANDSHAKE_REQUEST_SIZE - nn_consumed,
        nn_consumed, &nsize);
    if (MessageChain::error_ok != ret || nsize != nn_consumed) {
        return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, 
            "read c0c1, size=%d", nsize);
    }
  }
  return srs_success;
}

srs_error_t HandshakeHelper::Handles0s1s2(MessageChain* msg) {  
  uint32_t nsize;
  if (MessageChain::error_ok != msg->Read(
      s0s1s2, HANDSHAKE_RESPONSE_SIZE, &nsize) ||
      HANDSHAKE_RESPONSE_SIZE != nsize) {
    return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, 
        "read s0s1s2, size=%d", nsize);
  }
  return srs_success;
}

srs_error_t HandshakeHelper::Handlec2(MessageChain* msg) {
  uint32_t nsize;
  if (MessageChain::error_ok != msg->Read(
      c2, HANDSHAKE_ACK_SIZE, &nsize) || 
      HANDSHAKE_ACK_SIZE != nsize) {
    return srs_error_new(ERROR_RTMP_MSG_INVALID_SIZE, 
        "read c2, size=%d", nsize);
  }
  return srs_success;
}

srs_error_t HandshakeHelper::Createc0c1() {
  srs_random_generate(c0c1, HANDSHAKE_REQUEST_SIZE);
  
  // plain text required.
  SrsBuffer stream(c0c1, 9);
  
  stream.write_1bytes(0x03);
  stream.write_4bytes((int32_t)::time(NULL));
  stream.write_4bytes(0x00);
  
  return srs_success;
}

srs_error_t HandshakeHelper::Creates0s1s2(const char* c1) {
  srs_random_generate(s0s1s2, HANDSHAKE_RESPONSE_SIZE);
  
  // plain text required.
  SrsBuffer stream(s0s1s2, 9);
  
  stream.write_1bytes(0x03);
  stream.write_4bytes((int32_t)::time(NULL));
  // s1 time2 copy from c1
  if (c0c1) {
    stream.write_bytes(c0c1 + 1, 4);
  }
  
  // if c1 specified, copy c1 to s2.
  // @see: https://github.com/ossrs/srs/issues/46
  if (c1) {
    memcpy(s0s1s2 + HANDSHAKE_REQUEST_SIZE, c1, HANDSHAKE_ACK_SIZE);
  }
  
  return srs_success;
}

srs_error_t HandshakeHelper::Createc2() {
  srs_random_generate(c2, HANDSHAKE_ACK_SIZE);
  
  // time
  SrsBuffer stream(c2, 8);
  
  stream.write_4bytes((int32_t)::time(NULL));
  // c2 time2 copy from s1
  if (s0s1s2) {
    stream.write_bytes(s0s1s2 + 1, 4);
  }
  
  return srs_success;
}

////////////////////////////////////////////////////////////////////////////////
//SimpleRtmpHandshake
////////////////////////////////////////////////////////////////////////////////
srs_error_t SimpleRtmpHandshake::ServerHandshakeWithClient(
    HandshakeHelper* helper, MessageChain* msg, 
    RtmpBufferIO* sender) {
  srs_error_t err = srs_success;
  
  if ((err = helper->Handlec0c1(msg)) != srs_success) {
    return srs_error_wrap(err, "read c0c1");
  }
  
  // plain text required.
  if (helper->c0c1[0] != 0x03) {
    return srs_error_new(ERROR_RTMP_PLAIN_REQUIRED, 
        "only support rtmp plain text, version=%X", (uint8_t)helper->c0c1[0]);
  }
  
  if ((err = helper->Creates0s1s2(helper->c0c1 + 1)) != srs_success) {
    return srs_error_wrap(err, "create s0s1s2");
  }
  
  MessageChain res(HANDSHAKE_RESPONSE_SIZE, helper->s0s1s2,
      MessageChain::DONT_DELETE, HANDSHAKE_RESPONSE_SIZE);
  sender->Write(&res);
  return err;
}

srs_error_t SimpleRtmpHandshake::OnClientAck(
    HandshakeHelper* helper, MessageChain* msg) {
  srs_error_t err = srs_success;
  if ((err = helper->Handlec2(msg)) != srs_success) {
    return srs_error_wrap(err, "read c2");
  }
  
  MLOG_TRACE("simple handshake success.");
  return err;
}

srs_error_t SimpleRtmpHandshake::ClientHandshakeWithServer(
    HandshakeHelper* helper, RtmpBufferIO* sender) {
  srs_error_t err = srs_success;
  
  // simple handshake
  if ((err = helper->Createc0c1()) != srs_success) {
    return srs_error_wrap(err, "create c0c1");
  }
  
  MessageChain req(HANDSHAKE_REQUEST_SIZE, helper->c0c1,
      MessageChain::DONT_DELETE, HANDSHAKE_REQUEST_SIZE);
  sender->Write(&req);
  return err;
}

srs_error_t SimpleRtmpHandshake::OnServerAck(
    HandshakeHelper* helper, MessageChain* msg, 
    RtmpBufferIO* sender) {
  srs_error_t err = srs_success;    
  if ((err = helper->Handles0s1s2(msg)) != srs_success) {
    return srs_error_wrap(err, "read s0s1s2");
  }
  
  // plain text required.
  if (helper->s0s1s2[0] != 0x03) {
    return srs_error_new(ERROR_RTMP_HANDSHAKE, 
        "handshake failed, plain text required, version=%X",
        (uint8_t)helper->s0s1s2[0]);
  }
  
  if ((err = helper->Createc2()) != srs_success) {
    return srs_error_wrap(err, "create c2");
  }
  
  // for simple handshake, copy s1 to c2.
  // @see https://github.com/ossrs/srs/issues/418
  memcpy(helper->c2, helper->s0s1s2 + 1, HANDSHAKE_ACK_SIZE);
  MessageChain mcc2(HANDSHAKE_ACK_SIZE, helper->c2, 
      MessageChain::DONT_DELETE, HANDSHAKE_ACK_SIZE);
  sender->Write(&mcc2);
  
  MLOG_TRACE("simple handshake success.");
  return err;
}

void MediaRtmpHandshake::OnWrite() {
  assert(false);
}

////////////////////////////////////////////////////////////////////////////////
//ComplexRtmpHandshake
////////////////////////////////////////////////////////////////////////////////
srs_error_t ComplexRtmpHandshake::ServerHandshakeWithClient(
    HandshakeHelper* helper, MessageChain* msg, 
    RtmpBufferIO* sender) {
  srs_error_t err = srs_success;

  if ((err = helper->Handlec0c1(msg)) != srs_success) {
    return srs_error_wrap(err, "handle c0c1");
  }
  
  // decode c1
  c1s1 c1;
  // try schema0.
  // @remark, use schema0 to make flash player happy.
  if ((err = c1.parse(
      helper->c0c1 + 1, HANDSHAKE_ACK_SIZE, srs_schema0)) != srs_success) {
    return srs_error_wrap(err, "parse c1, schema=%d", srs_schema0);
  }
  // try schema1
  bool is_valid = false;
  if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
    delete err;
    
    if ((err=c1.parse(
        helper->c0c1 + 1, HANDSHAKE_ACK_SIZE, srs_schema1)) != srs_success) {
      return srs_error_wrap(err, "parse c0c1, schame=%d", srs_schema1);
    }
    
    if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
      delete err;
      return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, 
          "all schema valid failed, try simple handshake");
    }
  }
  
  // encode s1
  c1s1 s1;
  if ((err = s1.s1_create(&c1)) != srs_success) {
    return srs_error_wrap(err, "create s1 from c1");
  }
  // verify s1
  if ((err = s1.s1_validate_digest(is_valid)) != srs_success || !is_valid) {
    delete err;
    return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, 
        "verify s1 failed, try simple handshake");
  }
  
  c2s2 s2;
  if ((err = s2.s2_create(&c1)) != srs_success) {
    return srs_error_wrap(err, "create s2 from c1");
  }
  // verify s2
  if ((err = s2.s2_validate(&c1, is_valid)) != srs_success || !is_valid) {
    delete err;
    return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, 
        "verify s2 failed, try simple handshake");
  }
  
  // sendout s0s1s2
  if ((err = helper->Creates0s1s2()) != srs_success) {
    return srs_error_wrap(err, "create s0s1s2");
  }
  if ((err = s1.dump(helper->s0s1s2 + 1, HANDSHAKE_ACK_SIZE)) != srs_success) {
    return srs_error_wrap(err, "dump s1");
  }
  if ((err = s2.dump(helper->s0s1s2 + HANDSHAKE_REQUEST_SIZE,
     HANDSHAKE_ACK_SIZE)) != srs_success) {
    return srs_error_wrap(err, "dump s2");
  }

  MessageChain mcs(HANDSHAKE_RESPONSE_SIZE, helper->s0s1s2,
      MessageChain::DONT_DELETE, HANDSHAKE_RESPONSE_SIZE);

  sender->Write(&mcs);
  return err;
}

srs_error_t ComplexRtmpHandshake::OnClientAck(HandshakeHelper* helper, 
    MessageChain* msg) {
  srs_error_t err = srs_success;
  // recv c2
  if ((err = helper->Handlec2(msg)) != srs_success) {
    return srs_error_wrap(err, "read c2");
  }
  c2s2 c2;
  if ((err = c2.parse(helper->c2, HANDSHAKE_ACK_SIZE)) != srs_success) {
    return srs_error_wrap(err, "parse c2");
  }
  
  // verify c2
  // never verify c2, for ffmpeg will failed.
  // it's ok for flash.
  MLOG_TRACE("complex handshake success");
  return err;
}

srs_error_t ComplexRtmpHandshake::ClientHandshakeWithServer(
    HandshakeHelper* helper, RtmpBufferIO* sender) {
  srs_error_t err = srs_success;
  
  // complex handshake
  if ((err = helper->Createc0c1()) != srs_success) {
    return srs_error_wrap(err, "create c0c1");
  }
  
  // sign c1
  // @remark, FMS requires the schema1(digest-key), or connect failed.
  if ((err = c1.c1_create(srs_schema1)) != srs_success) {
    return srs_error_wrap(err, "create c1");
  }
  if ((err = c1.dump(helper->c0c1 + 1, HANDSHAKE_ACK_SIZE)) != srs_success) {
    return srs_error_wrap(err, "dump c1");
  }
  
  // verify c1
  bool is_valid;
  if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
    delete err;
    return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, "try simple handshake");
  }
  
  MessageChain mcr(HANDSHAKE_REQUEST_SIZE, helper->c0c1, 
      MessageChain::DONT_DELETE, HANDSHAKE_REQUEST_SIZE);
  sender->Write(&mcr);
 
  return err;
}

srs_error_t ComplexRtmpHandshake::OnServerAck(HandshakeHelper* helper,
    MessageChain* msg, RtmpBufferIO* sender) {
  srs_error_t err = srs_success;
  // s0s1s2
  if ((err = helper->Handles0s1s2(msg)) != srs_success) {
    return srs_error_wrap(err, "read s0s1s2");
  }
  
  // plain text required.
  if (helper->s0s1s2[0] != 0x03) {
    return srs_error_new(ERROR_RTMP_HANDSHAKE,  
        "handshake failed, plain text required, version=%X", 
        (uint8_t)helper->s0s1s2[0]);
  }
  
  // verify s1s2
  c1s1 s1;
  if ((err=s1.parse(
      helper->s0s1s2 + 1, HANDSHAKE_ACK_SIZE, c1.schema())) != srs_success) {
    return srs_error_wrap(err, "parse s1");
  }
  
  // never verify the s1,
  // for if forward to nginx-rtmp, verify s1 will failed,
  // TODO: FIXME: find the handshake schema of nginx-rtmp.
  
  // c2
  if ((err = helper->Createc2()) != srs_success) {
    return srs_error_wrap(err, "create c2");
  }
  
  c2s2 c2;
  if ((err = c2.c2_create(&s1)) != srs_success) {
    return srs_error_wrap(err, "create c2");
  }
  
  if ((err = c2.dump(helper->c2, HANDSHAKE_ACK_SIZE)) != srs_success) {
    return srs_error_wrap(err, "dump c2");
  }
  int nsize = 0;
  MessageChain mc2(HANDSHAKE_ACK_SIZE, helper->c2, 
      MessageChain::DONT_DELETE, HANDSHAKE_ACK_SIZE);

  sender->Write(&mc2);
  
  MLOG_TRACE("complex handshake success.");
  return err;
}


////////////////////////////////////////////////////////////////////////////////
//MediaRtmpHandshake
////////////////////////////////////////////////////////////////////////////////
MediaRtmpHandshake::MediaRtmpHandshake() { }

MediaRtmpHandshake::~MediaRtmpHandshake() {
  Close();
  if (read_buffer_) 
    read_buffer_->DestroyChained();
}

srs_error_t MediaRtmpHandshake::Start(std::shared_ptr<IMediaIO> io) {
  srs_error_t err = srs_success;

  sender_ = std::make_shared<RtmpBufferIO>(std::move(io), this);
  helper_.reset(new HandshakeHelper);
  handshake_.reset(new ComplexRtmpHandshake);

  return srs_success;
}

void MediaRtmpHandshake::Close() {
  sender_ = nullptr;
  helper_.reset(nullptr);
  handshake_.reset(nullptr);
  
  waiting_ack_ = false;
}

uint32_t MediaRtmpHandshake::ProxyRealIp() {
  return helper_->proxy_real_ip;
}

void MediaRtmpHandshake::OnDisc(int reason) {
  SignalHandshakefailed_(reason);
}

void MediaRtmpHandshakeS::OnRead(MessageChain* msg) {
  srs_error_t err = srs_success;

  if (!read_buffer_) {
    read_buffer_ = msg->DuplicateChained();
  } else {
    read_buffer_->Append(msg->DuplicateChained());
  }

  if (waiting_ack_) {
    // checking c2 has been arrived.
    if (read_buffer_->GetChainedLength() < HANDSHAKE_ACK_SIZE)
      return ;

    err = handshake_->OnClientAck(helper_.get(), read_buffer_);
    if (srs_success != err) {
      MLOG_ERROR("handshake failed desc:" << srs_error_desc(err));
      SignalHandshakefailed_(srs_error_code(err));
      delete err;
    } else {
      // maybe app data arrived

      read_buffer_ = read_buffer_->ReclaimGarbage();
      SignalHandshakeDone_(helper_->proxy_real_ip, 
          read_buffer_, std::move(sender_));
    }
    return ;
  }

  // server mode, at least c0 received
  if (read_buffer_->GetChainedLength() < HANDSHAKE_REQUEST_SIZE) {
    return ;
  }

  read_buffer_->SaveChainedReadPtr();
  // try complex handshake first
  err = handshake_->ServerHandshakeWithClient(
      helper_.get(), read_buffer_, sender_.get());

  // try simple handshake
  if (srs_success != err && srs_error_code(err) == ERROR_RTMP_TRY_SIMPLE_HS) {
    delete err;
    handshake_.reset(new SimpleRtmpHandshake);
    read_buffer_->RewindChained(true);
    err = handshake_->ServerHandshakeWithClient(
        helper_.get(), read_buffer_, sender_.get());
  }

  if (srs_success != err) {
    if (ERROR_SOCKET_WOULD_BLOCK != srs_error_code(err)) {
      SignalHandshakefailed_(srs_error_code(err));
      MLOG_ERROR("handshake failed, desc:" << srs_error_desc(err));
      delete err;
      return ;
    }
  }

  waiting_ack_ = true;
  read_buffer_ = read_buffer_->ReclaimGarbage();
}

//MediaRtmpHandshakeC
srs_error_t MediaRtmpHandshakeC::Start(std::shared_ptr<IMediaIO> io) {
  MediaRtmpHandshake::Start(std::move(io));

  return handshake_->ClientHandshakeWithServer(
      helper_.get(), sender_.get());
}

void MediaRtmpHandshakeC::OnRead(MessageChain*) {

}

} //namespace ma
