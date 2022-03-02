#include "media_flv_decoder.h"

#include "common/media_io.h"

namespace ma {

SrsFlvDecoder::SrsFlvDecoder() {
  reader = NULL;
}

SrsFlvDecoder::~SrsFlvDecoder() { }

srs_error_t SrsFlvDecoder::initialize(ISrsReader* fr) {
  srs_assert(fr);
  reader = fr;
  return srs_success;
}

srs_error_t SrsFlvDecoder::read_header(char header[9]) {
  srs_error_t err = srs_success;
  srs_assert(header);
  
  // TODO: FIXME: Should use readfully.
  if ((err = reader->read(header, 9, NULL)) != srs_success) {
    return srs_error_wrap(err, "read header");
  }
  
  char* h = header;
  if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
    return srs_error_new(ERROR_KERNEL_FLV_HEADER, "flv header must start with FLV");
  }
  
  return err;
}

srs_error_t SrsFlvDecoder::read_tag_header(
    char* ptype, int32_t* pdata_size, uint32_t* ptime) {
  srs_error_t err = srs_success;
  
  srs_assert(ptype);
  srs_assert(pdata_size);
  srs_assert(ptime);
  
  char th[11]; // tag header
  
  // read tag header
  // TODO: FIXME: Should use readfully.
  if ((err = reader->read(th, 11, NULL)) != srs_success) {
    return srs_error_wrap(err, "read flv tag header failed");
  }
  
  // Reserved UB [2]
  // Filter UB [1]
  // TagType UB [5]
  *ptype = (th[0] & 0x1F);
  
  // DataSize UI24
  char* pp = (char*)pdata_size;
  pp[3] = 0;
  pp[2] = th[1];
  pp[1] = th[2];
  pp[0] = th[3];
  
  // Timestamp UI24
  pp = (char*)ptime;
  pp[2] = th[4];
  pp[1] = th[5];
  pp[0] = th[6];
  
  // TimestampExtended UI8
  pp[3] = th[7];
  
  return err;
}

srs_error_t SrsFlvDecoder::read_tag_data(char* data, int32_t size) {
  srs_error_t err = srs_success;
  srs_assert(data);
  
  // TODO: FIXME: Should use readfully.
  if ((err = reader->read(data, size, NULL)) != srs_success) {
    return srs_error_wrap(err, "read flv tag header failed");
  }
  
  return err;
}

srs_error_t SrsFlvDecoder::read_previous_tag_size(char previous_tag_size[4]) {
  srs_error_t err = srs_success;
  srs_assert(previous_tag_size);
  
  // ignore 4bytes tag size.
  // TODO: FIXME: Should use readfully.
  if ((err = reader->read(previous_tag_size, 4, NULL)) != srs_success) {
    return srs_error_wrap(err, "read flv previous tag size failed");
  }
  
  return err;
}

}

