//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_AMF0_HPP
#define SRS_PROTOCOL_AMF0_HPP

#include <string>
#include <vector>

#include "common/media_kernel_error.h"

namespace ma {

class SrsBuffer;
class RtmpAmf0Object;
class RtmpAmf0EcmaArray;
class RtmpAmf0StrictArray;
class SrsJsonAny;

// internal objects, user should never use it.
namespace srs_internal
{
    class RtmpUnSortedHashtable;
    class RtmpAmf0ObjectEOF;
    class RtmpAmf0Date;
}

/*
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 Usages:
 
 1. the bytes proxy: SrsBuffer
 // when we got some bytes from file or network,
 // use SrsBuffer proxy to read/write bytes
 
 // for example, read bytes from file or network.
 char* bytes = ...;
 
 // initialize the stream, proxy for bytes.
 SrsBuffer stream;
 stream.initialize(bytes);
 
 // use stream instead.
 
 2. directly read AMF0 any instance from stream:
 RtmpAmf0Any* pany = NULL;
 srs_amf0_read_any(&stream, &pany);
 
 3. use RtmpAmf0Any to discovery instance from stream:
 RtmpAmf0Any* pany = NULL;
 RtmpAmf0Any::discovery(&stream, &pany);
 
 4. directly read specified AMF0 instance value from stream:
 string value;
 srs_amf0_read_string(&stream, value);
 
 5. directly read specified AMF0 instance from stream:
 RtmpAmf0Any* str = RtmpAmf0Any::str();
 str->read(&stream);
 
 6. get value from AMF0 instance:
 // parse or set by other user
 RtmpAmf0Any* any = ...;
 
 if (any->is_string()) {
 string str = any->to_string();
 }
 
 7. get complex object from AMF0 insance:
 // parse or set by other user
 RtmpAmf0Any* any = ...;
 
 if (any->is_object()) {
 RtmpAmf0Object* obj = any->to_object();
 obj->set("width", RtmpAmf0Any::number(1024));
 obj->set("height", RtmpAmf0Any::number(576));
 }
 
 8. serialize AMF0 instance to bytes:
 // parse or set by other user
 RtmpAmf0Any* any = ...;
 
 char* bytes = new char[any->total_size()];
 
 SrsBuffer stream;
 stream.initialize(bytes);
 
 any->write(&stream);
 
 @remark: for detail usage, see interfaces of each object.
 @remark: all examples ignore the error process.
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 ////////////////////////////////////////////////////////////////////////
 */

/**
 * any amf0 value.
 * 2.1 Types Overview
 * value-type = number-type | boolean-type | string-type | object-type
 *         | null-marker | undefined-marker | reference-type | ecma-array-type
 *         | strict-array-type | date-type | long-string-type | xml-document-type
 *         | typed-object-type
 */
class RtmpAmf0Any {
 public:
  RtmpAmf0Any();
  virtual ~RtmpAmf0Any();
  // type identify, user should identify the type then convert from/to value.

  /**
   * whether current instance is an AMF0 string.
   * @return true if instance is an AMF0 string; otherwise, false.
   * @remark, if true, use to_string() to get its value.
   */
  virtual bool is_string();
  /**
   * whether current instance is an AMF0 boolean.
   * @return true if instance is an AMF0 boolean; otherwise, false.
   * @remark, if true, use to_boolean() to get its value.
   */
  virtual bool is_boolean();
  /**
   * whether current instance is an AMF0 number.
   * @return true if instance is an AMF0 number; otherwise, false.
   * @remark, if true, use to_number() to get its value.
   */
  virtual bool is_number();
  /**
   * whether current instance is an AMF0 null.
   * @return true if instance is an AMF0 null; otherwise, false.
   */
  virtual bool is_null();
  /**
   * whether current instance is an AMF0 undefined.
   * @return true if instance is an AMF0 undefined; otherwise, false.
   */
  virtual bool is_undefined();
  /**
   * whether current instance is an AMF0 object.
   * @return true if instance is an AMF0 object; otherwise, false.
   * @remark, if true, use to_object() to get its value.
   */
  virtual bool is_object();
  /**
   * whether current instance is an AMF0 object-EOF.
   * @return true if instance is an AMF0 object-EOF; otherwise, false.
   */
  virtual bool is_object_eof();
  /**
   * whether current instance is an AMF0 ecma-array.
   * @return true if instance is an AMF0 ecma-array; otherwise, false.
   * @remark, if true, use to_ecma_array() to get its value.
   */
  virtual bool is_ecma_array();
  /**
   * whether current instance is an AMF0 strict-array.
   * @return true if instance is an AMF0 strict-array; otherwise, false.
   * @remark, if true, use to_strict_array() to get its value.
   */
  virtual bool is_strict_array();
  /**
   * whether current instance is an AMF0 date.
   * @return true if instance is an AMF0 date; otherwise, false.
   * @remark, if true, use to_date() to get its value.
   */
  virtual bool is_date();
  /**
   * whether current instance is an AMF0 object, object-EOF, ecma-array or strict-array.
   */
  virtual bool is_complex_object();
  // get value of instance
 public:
  /**
   * get a string copy of instance.
   * @remark assert is_string(), user must ensure the type then convert.
   */
  virtual std::string to_str();
  /**
   * get the raw str of instance,
   * user can directly set the content of str.
   * @remark assert is_string(), user must ensure the type then convert.
   */
  virtual const char* to_str_raw();
  /**
   * convert instance to amf0 boolean,
   * @remark assert is_boolean(), user must ensure the type then convert.
   */
  virtual bool to_boolean();
  /**
   * convert instance to amf0 number,
   * @remark assert is_number(), user must ensure the type then convert.
   */
  virtual double to_number();
  /**
   * convert instance to date,
   * @remark assert is_date(), user must ensure the type then convert.
   */
  virtual int64_t to_date();
  virtual int16_t to_date_time_zone();
  /**
   * convert instance to amf0 object,
   * @remark assert is_object(), user must ensure the type then convert.
   */
  virtual RtmpAmf0Object* to_object();
  /**
   * convert instance to ecma array,
   * @remark assert is_ecma_array(), user must ensure the type then convert.
   */
  virtual RtmpAmf0EcmaArray* to_ecma_array();
  /**
   * convert instance to strict array,
   * @remark assert is_strict_array(), user must ensure the type then convert.
   */
  virtual RtmpAmf0StrictArray* to_strict_array();
  // set value of instance
 public:
  /**
   * set the number of any when is_number() indicates true.
   * user must ensure the type is a number, or assert failed.
   */
  virtual void set_number(double value);
  // serialize/deseriaize instance.

  /**
   * get the size of amf0 any, including the marker size.
   * the size is the bytes which instance serialized to.
   */
  virtual int total_size() = 0;
  /**
   * read AMF0 instance from stream.
   */
  virtual srs_error_t read(SrsBuffer* stream) = 0;
  /**
   * write AMF0 instance to stream.
   */
  virtual srs_error_t write(SrsBuffer* stream) = 0;
  /**
   * copy current AMF0 instance.
   */
  virtual RtmpAmf0Any* copy() = 0;
  /**
   * human readable print
   * @param pdata, output the heap data, NULL to ignore.
   * @return return the *pdata for print. NULL to ignore.
   * @remark user must free the data returned or output by pdata.
   */
  virtual char* human_print(char** pdata, int* psize);
  /**
   * convert amf0 to json.
   */
  virtual SrsJsonAny* to_json();
  // create AMF0 instance.

  /**
   * create an AMF0 string instance, set string content by value.
   */
  static RtmpAmf0Any* str(const char* value = NULL);
  /**
   * create an AMF0 boolean instance, set boolean content by value.
   */
  static RtmpAmf0Any* boolean(bool value = false);
  /**
   * create an AMF0 number instance, set number content by value.
   */
  static RtmpAmf0Any* number(double value = 0.0);
  /**
   * create an AMF0 date instance
   */
  static RtmpAmf0Any* date(int64_t value = 0);
  /**
   * create an AMF0 null instance
   */
  static RtmpAmf0Any* null();
  /**
   * create an AMF0 undefined instance
   */
  static RtmpAmf0Any* undefined();
  /**
   * create an AMF0 empty object instance
   */
  static RtmpAmf0Object* object();
  /**
   * create an AMF0 object-EOF instance
   */
  static RtmpAmf0Any* object_eof();
  /**
   * create an AMF0 empty ecma-array instance
   */
  static RtmpAmf0EcmaArray* ecma_array();
  /**
   * create an AMF0 empty strict-array instance
   */
  static RtmpAmf0StrictArray* strict_array();
  // discovery instance from stream

  /**
   * discovery AMF0 instance from stream
   * @param ppvalue, output the discoveried AMF0 instance.
   *       NULL if error.
   * @remark, instance is created without read from stream, user must
   *       use (*ppvalue)->read(stream) to get the instance.
   */
  static srs_error_t discovery(SrsBuffer* stream, RtmpAmf0Any** ppvalue);
 public:
  char marker;
};

/**
 * 2.5 Object Type
 * anonymous-object-type = object-marker *(object-property)
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class RtmpAmf0Object : public RtmpAmf0Any {
 private:
  srs_internal::RtmpUnSortedHashtable* properties;
  srs_internal::RtmpAmf0ObjectEOF* eof;
 private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 object to private,
   * use should never declare it, use RtmpAmf0Any::object() to create it.
   */
  RtmpAmf0Object();
 public:
  virtual ~RtmpAmf0Object();
  // serialize/deserialize to/from stream.
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
  /**
   * convert amf0 to json.
   */
  virtual SrsJsonAny* to_json();
// properties iteration
public:
  /**
   * clear all propergies.
   */
  virtual void clear();
  /**
   * get the count of properties(key:value).
   */
  virtual int count();
  /**
   * get the property(key:value) key at index.
   * @remark: max index is count().
   */
  virtual std::string key_at(int index);
  /**
   * get the property(key:value) key raw bytes at index.
   * user can directly set the key bytes.
   * @remark: max index is count().
   */
  virtual const char* key_raw_at(int index);
  /**
   * get the property(key:value) value at index.
   * @remark: max index is count().
   */
  virtual RtmpAmf0Any* value_at(int index);
  // property set/get.
 public:
  /**
   * set the property(key:value) of object,
   * @param key, string property name.
   * @param value, an AMF0 instance property value.
   * @remark user should never free the value, this instance will manage it.
   */
  virtual void set(std::string key, RtmpAmf0Any* value);
  /**
   * get the property(key:value) of object,
   * @param name, the property name/key
   * @return the property AMF0 value, NULL if not found.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* get_property(std::string name);
  /**
   * get the string property, ensure the property is_string().
   * @return the property AMF0 value, NULL if not found, or not a string.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* ensure_property_string(std::string name);
  /**
   * get the number property, ensure the property is_number().
   * @return the property AMF0 value, NULL if not found, or not a number.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* ensure_property_number(std::string name);
  /**
   * remove the property specified by name.
   */
  virtual void remove(std::string name);
};

/**
 * 2.10 ECMA Array Type
 * ecma-array-type = associative-count *(object-property)
 * associative-count = U32
 * object-property = (UTF-8 value-type) | (UTF-8-empty object-end-marker)
 */
class RtmpAmf0EcmaArray : public RtmpAmf0Any {
 private:
  srs_internal::RtmpUnSortedHashtable* properties;
  srs_internal::RtmpAmf0ObjectEOF* eof;
  int32_t _count;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 object to private,
   * use should never declare it, use RtmpAmf0Any::ecma_array() to create it.
   */
  RtmpAmf0EcmaArray();
 public:
  virtual ~RtmpAmf0EcmaArray();
  // serialize/deserialize to/from stream.
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
  /**
   * convert amf0 to json.
   */
  virtual SrsJsonAny* to_json();
// properties iteration
 public:
  /**
   * clear all propergies.
   */
  virtual void clear();
  /**
   * get the count of properties(key:value).
   */
  virtual int count();
  /**
   * get the property(key:value) key at index.
   * @remark: max index is count().
   */
  virtual std::string key_at(int index);
  /**
   * get the property(key:value) key raw bytes at index.
   * user can directly set the key bytes.
   * @remark: max index is count().
   */
  virtual const char* key_raw_at(int index);
  /**
   * get the property(key:value) value at index.
   * @remark: max index is count().
   */
  virtual RtmpAmf0Any* value_at(int index);
  // property set/get.
public:
  /**
   * set the property(key:value) of array,
   * @param key, string property name.
   * @param value, an AMF0 instance property value.
   * @remark user should never free the value, this instance will manage it.
   */
  virtual void set(std::string key, RtmpAmf0Any* value);
  /**
   * get the property(key:value) of array,
   * @param name, the property name/key
   * @return the property AMF0 value, NULL if not found.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* get_property(std::string name);
  /**
   * get the string property, ensure the property is_string().
   * @return the property AMF0 value, NULL if not found, or not a string.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* ensure_property_string(std::string name);
  /**
   * get the number property, ensure the property is_number().
   * @return the property AMF0 value, NULL if not found, or not a number.
   * @remark user should never free the returned value, copy it if needed.
   */
  virtual RtmpAmf0Any* ensure_property_number(std::string name);
};

/**
 * 2.12 Strict Array Type
 * array-count = U32
 * strict-array-type = array-count *(value-type)
 */
class RtmpAmf0StrictArray : public RtmpAmf0Any {
private:
  std::vector<RtmpAmf0Any*> properties;
  int32_t _count;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 object to private,
   * use should never declare it, use RtmpAmf0Any::strict_array() to create it.
   */
  RtmpAmf0StrictArray();
 public:
  virtual ~RtmpAmf0StrictArray();
  // serialize/deserialize to/from stream.
 public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
    /**
     * convert amf0 to json.
     */
    virtual SrsJsonAny* to_json();
 // properties iteration
 public:
  /**
   * clear all elements.
   */
  virtual void clear();
  /**
   * get the count of elements
   */
  virtual int count();
  /**
   * get the elements key at index.
   * @remark: max index is count().
   */
  virtual RtmpAmf0Any* at(int index);
  // property set/get.
 public:
  /**
   * append new element to array
   * @param any, an AMF0 instance property value.
   * @remark user should never free the any, this instance will manage it.
   */
  virtual void append(RtmpAmf0Any* any);
};

/**
 * the class to get amf0 object size
 */
class RtmpAmf0Size {
 public:
  static int utf8(std::string value);
  static int str(std::string value);
  static int number();
  static int date();
  static int null();
  static int undefined();
  static int boolean();
  static int object(RtmpAmf0Object* obj);
  static int object_eof();
  static int ecma_array(RtmpAmf0EcmaArray* arr);
  static int strict_array(RtmpAmf0StrictArray* arr);
  static int any(RtmpAmf0Any* o);
};

/**
 * read anything from stream.
 * @param ppvalue, the output amf0 any elem.
 *         NULL if error; otherwise, never NULL and user must free it.
 */
extern srs_error_t srs_amf0_read_any(SrsBuffer* stream, RtmpAmf0Any** ppvalue);

/**
 * read amf0 string from stream.
 * 2.4 String Type
 * string-type = string-marker UTF-8
 */
extern srs_error_t srs_amf0_read_string(SrsBuffer* stream, std::string& value);
extern srs_error_t srs_amf0_write_string(SrsBuffer* stream, std::string value);

/**
 * read amf0 boolean from stream.
 * 2.4 String Type
 * boolean-type = boolean-marker U8
 *         0 is false, <> 0 is true
 */
extern srs_error_t srs_amf0_read_boolean(SrsBuffer* stream, bool& value);
extern srs_error_t srs_amf0_write_boolean(SrsBuffer* stream, bool value);

/**
 * read amf0 number from stream.
 * 2.2 Number Type
 * number-type = number-marker DOUBLE
 */
extern srs_error_t srs_amf0_read_number(SrsBuffer* stream, double& value);
extern srs_error_t srs_amf0_write_number(SrsBuffer* stream, double value);

/**
 * read amf0 null from stream.
 * 2.7 null Type
 * null-type = null-marker
 */
extern srs_error_t srs_amf0_read_null(SrsBuffer* stream);
extern srs_error_t srs_amf0_write_null(SrsBuffer* stream);

/**
 * read amf0 undefined from stream.
 * 2.8 undefined Type
 * undefined-type = undefined-marker
 */
extern srs_error_t srs_amf0_read_undefined(SrsBuffer* stream);
extern srs_error_t srs_amf0_write_undefined(SrsBuffer* stream);

// internal objects, user should never use it.
namespace srs_internal {
/**
 * read amf0 string from stream.
 * 2.4 String Type
 * string-type = string-marker UTF-8
 * @return default value is empty string.
 * @remark: use RtmpAmf0Any::str() to create it.
 */
class RtmpAmf0String : public RtmpAmf0Any {
public:
  std::string value;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 string to private,
   * use should never declare it, use RtmpAmf0Any::str() to create it.
   */
  RtmpAmf0String(const char* _value);
public:
  virtual ~RtmpAmf0String();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * read amf0 boolean from stream.
 * 2.4 String Type
 * boolean-type = boolean-marker U8
 *         0 is false, <> 0 is true
 * @return default value is false.
 */
class RtmpAmf0Boolean : public RtmpAmf0Any {
public:
  bool value;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 boolean to private,
   * use should never declare it, use RtmpAmf0Any::boolean() to create it.
   */
  RtmpAmf0Boolean(bool _value);
public:
  virtual ~RtmpAmf0Boolean();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * read amf0 number from stream.
 * 2.2 Number Type
 * number-type = number-marker DOUBLE
 * @return default value is 0.
 */
class RtmpAmf0Number : public RtmpAmf0Any {
public:
  double value;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 number to private,
   * use should never declare it, use RtmpAmf0Any::number() to create it.
   */
  RtmpAmf0Number(double _value);
public:
  virtual ~RtmpAmf0Number();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * 2.13 Date Type
 * time-zone = S16 ; reserved, not supported should be set to 0x0000
 * date-type = date-marker DOUBLE time-zone
 * @see: https://github.com/ossrs/srs/issues/185
 */
class RtmpAmf0Date : public RtmpAmf0Any {
private:
  int64_t _date_value;
  int16_t _time_zone;
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 date to private,
   * use should never declare it, use RtmpAmf0Any::date() to create it.
   */
  RtmpAmf0Date(int64_t value);
public:
  virtual ~RtmpAmf0Date();
  // serialize/deserialize to/from stream.
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
public:
  /**
   * get the date value.
   */
  virtual int64_t date();
  /**
   * get the time_zone.
   */
  virtual int16_t time_zone();
};

/**
 * read amf0 null from stream.
 * 2.7 null Type
 * null-type = null-marker
 */
class RtmpAmf0Null : public RtmpAmf0Any {
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 null to private,
   * use should never declare it, use RtmpAmf0Any::null() to create it.
   */
  RtmpAmf0Null();
public:
  virtual ~RtmpAmf0Null();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * read amf0 undefined from stream.
 * 2.8 undefined Type
 * undefined-type = undefined-marker
 */
class RtmpAmf0Undefined : public RtmpAmf0Any {
private:
  friend class RtmpAmf0Any;
  /**
   * make amf0 undefined to private,
   * use should never declare it, use RtmpAmf0Any::undefined() to create it.
   */
  RtmpAmf0Undefined();
public:
  virtual ~RtmpAmf0Undefined();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * to ensure in inserted order.
 * for the FMLE will crash when AMF0Object is not ordered by inserted,
 * if ordered in map, the string compare order, the FMLE will creash when
 * get the response of connect app.
 */
class RtmpUnSortedHashtable {
private:
  typedef std::pair<std::string, RtmpAmf0Any*> RtmpAmf0ObjectPropertyType;
  std::vector<RtmpAmf0ObjectPropertyType> properties;
public:
  RtmpUnSortedHashtable();
  virtual ~RtmpUnSortedHashtable();
public:
  virtual int count();
  virtual void clear();
  virtual std::string key_at(int index);
  virtual const char* key_raw_at(int index);
  virtual RtmpAmf0Any* value_at(int index);
  /**
   * set the value of hashtable.
   * @param value, the value to set. NULL to delete the property.
   */
  virtual void set(std::string key, RtmpAmf0Any* value);
public:
  virtual RtmpAmf0Any* get_property(std::string name);
  virtual RtmpAmf0Any* ensure_property_string(std::string name);
  virtual RtmpAmf0Any* ensure_property_number(std::string name);
  virtual void remove(std::string name);
public:
  virtual void copy(RtmpUnSortedHashtable* src);
};

/**
 * 2.11 Object End Type
 * object-end-type = UTF-8-empty object-end-marker
 * 0x00 0x00 0x09
 */
class RtmpAmf0ObjectEOF : public RtmpAmf0Any {
public:
  RtmpAmf0ObjectEOF();
  virtual ~RtmpAmf0ObjectEOF();
public:
  virtual int total_size();
  virtual srs_error_t read(SrsBuffer* stream);
  virtual srs_error_t write(SrsBuffer* stream);
  virtual RtmpAmf0Any* copy();
};

/**
 * read amf0 utf8 string from stream.
 * 1.3.1 Strings and UTF-8
 * UTF-8 = U16 *(UTF8-char)
 * UTF8-char = UTF8-1 | UTF8-2 | UTF8-3 | UTF8-4
 * UTF8-1 = %x00-7F
 * @remark only support UTF8-1 char.
 */
extern srs_error_t srs_amf0_read_utf8(SrsBuffer* stream, std::string& value);
extern srs_error_t srs_amf0_write_utf8(SrsBuffer* stream, std::string value);

extern bool srs_amf0_is_object_eof(SrsBuffer* stream);
extern srs_error_t srs_amf0_write_object_eof(
    SrsBuffer* stream, RtmpAmf0ObjectEOF* value);

extern srs_error_t srs_amf0_write_any(SrsBuffer* stream, RtmpAmf0Any* value);
} //srs_internal

} //namespace ma

#endif
