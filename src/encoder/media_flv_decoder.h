//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_FLV_DECODER_H__
#define __MEDIA_FLV_DECODER_H__

#include "common/media_kernel_error.h"

namespace ma {

class ISrsReader;

// Decode flv file.
class SrsFlvDecoder
{
private:
    ISrsReader* reader;
public:
    SrsFlvDecoder();
    virtual ~SrsFlvDecoder();
public:
    // Initialize the underlayer file stream
    // @remark user can initialize multiple times to decode multiple flv files.
    // @remark user must free the @param fr, flv decoder never close/free it
    virtual srs_error_t initialize(ISrsReader* fr);
public:
    // Read the flv header, donot including the 4bytes previous tag size.
    // @remark assert header not NULL.
    virtual srs_error_t read_header(char header[9]);
    // Read the tag header infos.
    // @remark assert ptype/pdata_size/ptime not NULL.
    virtual srs_error_t read_tag_header(char* ptype, int32_t* pdata_size, uint32_t* ptime);
    // Read the tag data.
    // @remark assert data not NULL.
    virtual srs_error_t read_tag_data(char* data, int32_t size);
    // Read the 4bytes previous tag size.
    // @remark assert previous_tag_size not NULL.
    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4]);
};
}

#endif //!__MEDIA_FLV_DECODER_H__

