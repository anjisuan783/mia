#include "json.h"
#include <assert.h>
#include <stdlib.h>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <climits>
#include <string.h>
#include <functional>
#include <cctype>

#ifndef WIN32
#define _stricmp strcasecmp
#endif

#ifdef _MSC_VER
#define snprintf sprintf_s
#endif

using namespace json;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static std::string FrontTrim(const std::string& str)
{
	std::string s = str;
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
	return s;
}

static std::string BackTrim(const std::string& str)
{
	std::string s = str;
	s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
	return s;
}

static std::string Trim(const std::string& str)
{
	return FrontTrim(BackTrim(str));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Value::Value(const Value& v) : mValueType(v.mValueType)
{
	switch (mValueType)
	{
		case StringVal		: mStringVal = v.mStringVal; break;
		case IntVal			: mIntVal = v.mIntVal; mFloatVal = (float)v.mIntVal; mDoubleVal = (double)v.mIntVal; break;
		case FloatVal		: mFloatVal = v.mFloatVal; mIntVal = (int)v.mFloatVal; mDoubleVal = (double)v.mDoubleVal; break;
		case DoubleVal		: mDoubleVal = v.mDoubleVal; mIntVal = (int)v.mDoubleVal; mFloatVal = (float)v.mDoubleVal; break;
		case BoolVal		: mBoolVal = v.mBoolVal; break;
		case ObjectVal		: mObjectVal = v.mObjectVal; break;
		case ArrayVal		: mArrayVal = v.mArrayVal; break;
		default				: break;
	}
}

Value& Value::operator =(const Value& v)
{
	if (&v == this)
		return *this;

	mValueType = v.mValueType;

	switch (mValueType)
	{
		case StringVal		: mStringVal = v.mStringVal; break;
		case IntVal			: mIntVal = v.mIntVal; mFloatVal = (float)v.mIntVal; mDoubleVal = (double)v.mIntVal; break;
		case FloatVal		: mFloatVal = v.mFloatVal; mIntVal = (int)v.mFloatVal; mDoubleVal = (double)v.mDoubleVal; break;
		case DoubleVal		: mDoubleVal = v.mDoubleVal; mIntVal = (int)v.mDoubleVal; mFloatVal = (float)v.mDoubleVal; break;
		case BoolVal		: mBoolVal = v.mBoolVal; break;
		case ObjectVal		: mObjectVal = v.mObjectVal; break;
		case ArrayVal		: mArrayVal = v.mArrayVal; break;
		default				: break;
	}

	return *this;
}

Value& Value::operator [](size_t idx)
{
	assert(mValueType == ArrayVal);
	return mArrayVal[idx];
}

const Value& Value::operator [](size_t idx) const
{
	assert(mValueType == ArrayVal);
	return mArrayVal[idx];
}

Value& Value::operator [](const std::string& key)
{
	assert(mValueType == ObjectVal);
	return mObjectVal[key];
}

const Value& Value::operator [](const std::string& key) const
{
	assert(mValueType == ObjectVal);
	return mObjectVal[key];
}

void Value::Clear()
{
	mValueType = NULLVal;
}

size_t Value::size() const
{
	if ((mValueType != ObjectVal) && (mValueType != ArrayVal))
		return 1;

	return mValueType == ObjectVal ? mObjectVal.size() : mArrayVal.size();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Array::Array()
{
}

Array::Array(const Array& a) : mValues(a.mValues)
{
}

Array& Array::operator =(const Array& a)
{
	if (&a == this)
		return *this;

	Clear();
	mValues = a.mValues;

	return *this;
}

Value& Array::operator [](size_t i)
{
	return mValues[i];
}

const Value& Array::operator [](size_t i) const
{
	return mValues[i];
}


Array::ValueVector::const_iterator Array::begin() const
{
	return mValues.begin();
}

Array::ValueVector::const_iterator Array::end() const
{
	return mValues.end();
}

Array::ValueVector::iterator Array::begin()
{
	return mValues.begin();
}

Array::ValueVector::iterator Array::end()
{
	return mValues.end();
}

void Array::push_back(const Value& v)
{
	mValues.push_back(v);
}

void Array::insert(size_t index, const Value& v)
{
	mValues.insert(mValues.begin() + index, v);
}

size_t Array::size() const
{
	return mValues.size();
}

void Array::Clear()
{
	mValues.clear();
}

Array::ValueVector::iterator Array::find(const Value& v)
{
	return std::find(mValues.begin(), mValues.end(), v);
}

Array::ValueVector::const_iterator Array::find(const Value& v) const
{
	return std::find(mValues.begin(), mValues.end(), v);
}

bool Array::HasValue(const Value& v) const
{
	return find(v) != end();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Object::Object()
{
}

Object::Object(const Object& obj) : mValues(obj.mValues)
{

}

Object& Object::operator =(const Object& obj)
{
	if (&obj == this)
		return *this;

	Clear();
	mValues = obj.mValues;

	return *this;
}

Value& Object::operator [](const std::string& key)
{
	return mValues[key];
}

const Value& Object::operator [](const std::string& key) const
{
	ValueMap::const_iterator it = mValues.find(key);
	return it->second;
}

Object::ValueMap::const_iterator Object::begin() const
{
	return mValues.begin();
}

Object::ValueMap::const_iterator Object::end() const
{
	return mValues.end();
}

Object::ValueMap::iterator Object::begin()
{
	return mValues.begin();
}

Object::ValueMap::iterator Object::end()
{
	return mValues.end();
}

Object::ValueMap::iterator Object::find(const std::string& key)
{
	return mValues.find(key);
}

Object::ValueMap::const_iterator Object::find(const std::string& key) const
{
	return mValues.find(key);
}

bool Object::HasKey(const std::string& key) const
{
	return find(key) != end();
}

void Object::Clear()
{
	mValues.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
std::string SerializeArray(const Array& a);

std::string SerializeValue(const Value& v)
{
	std::string str;

	static const int BUFF_SZ = 500;
	char buff[BUFF_SZ];
	switch (v.GetType())
	{
		case IntVal			: snprintf(buff, BUFF_SZ, "%d", (int)v); str = buff; break;
		case FloatVal		: snprintf(buff, BUFF_SZ, "%f", (float)v); str = buff; break;
		case DoubleVal		: snprintf(buff, BUFF_SZ, "%f", (double)v); str = buff; break;
		case BoolVal		: str = v ? "true" : "false"; break;
		case NULLVal		: str = "null"; break;
		case ObjectVal		: str = Serialize(v); break;
		case ArrayVal		: str = SerializeArray(v); break;
		case StringVal		: str = std::string("\"") + (std::string)v + std::string("\""); break;
	}

	return str;
}

std::string SerializeArray(const Array& a)
{
	std::string str = "[";

	bool first = true;
	for (size_t i = 0; i < a.size(); i++)
	{
		const Value& v = a[i];
		if (!first)
			str += std::string(",");

		str += SerializeValue(v);

		first = false;
	}

	str += "]";
	return str;
}

std::string json::Serialize(const Object& obj)
{
	std::string str = "{";

	bool first = true;
	for (Object::ValueMap::const_iterator it = obj.begin(); it != obj.end(); it++)
	{
		if (!first)
			str += std::string(",");

		str += std::string("\"") + it->first + std::string("\":") + SerializeValue(it->second);
		first = false;
	}

	str += "}";
	return str;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Array DeserializeArray(std::string& str);

Value DeserializeValue(std::string& str)
{
	Value v;

	str = Trim(str);

	if (str.length() == 0)
		return v;

	if (str[0] == '[')
	{
		int depth = 1;
		size_t i = 1;
		for (; i < str.length(); i++)
		{
			if (str[i] == '[')
				++depth;
			else if ((str[i] == ']') && (--depth == 0))
				break;
		}
		
		//assert(depth == 0);
		if (depth != 0)
			return v;

		std::string array_str = str.substr(0, i + 1);
		v = Value(DeserializeArray(array_str));
		str = str.substr(i + 1, str.length());
	}
	else if (str[0] == '{')
	{
		int depth = 1;
		size_t i = 1;
		for (; i < str.length(); i++)
		{
			if (str[i] == '{')
				++depth;
			else if ((str[i] == '}') && (--depth == 0))
				break;
		}

		//assert(depth == 0);
		if (depth != 0)
			return v;

		std::string obj_str = str.substr(0, i + 1);
		v = Value(Deserialize(obj_str));
		str = str.substr(i + 1, str.length());
	}
	else if (str[0] == '\"')
	{
		size_t end_quote = str.find('\"', 1);
		//assert(end_quote != std::string::npos);
		if (end_quote == std::string::npos)
			return v;

		v = Value(str.substr(1, end_quote - 1));
		str = str.substr(end_quote + 1, str.length());
	}
	else
	{
		bool has_dot = false;
		bool has_e = false;
		std::string temp_val;
		size_t i = 0;
		for (; i < str.length(); i++)
		{
			if (str[i] == '.')
				has_dot = true;
			else if (str[i] == 'e')
				has_e = true;
			else if ((str[i] == ']') || (str[i] == '}') || (str[i] == ','))
				break;

			if (!std::isspace(str[i]))
				temp_val += str[i];
		}

		// store all floating point as doubles. This will also set the float and int values as well.
		if (_stricmp(temp_val.c_str(), "true") == 0)
			v = Value(true);
		else if (_stricmp(temp_val.c_str(), "false") == 0)
			v = Value(false);
		else if (has_e || has_dot)
			v = Value(atof(temp_val.c_str()));
		else if (_stricmp(temp_val.c_str(), "null") == 0)
			v = Value();
		else
		{
			// Check if the value is beyond the size of an int and if so, store it as a double
			double tmp_val = atof(temp_val.c_str());
			if ((tmp_val >= (double)INT_MIN) && (tmp_val <= (double)INT_MAX))
				v = Value(atoi(temp_val.c_str()));
			else
				v = Value(tmp_val);
		}

		str = str.substr(i, str.length());
	}

	return v;
}

Array DeserializeArray(std::string& str)
{
	Array a;

	str = Trim(str);

	//assert((str[0] == '[') && (str[str.length() - 1] == ']'));

	if ((str[0] == '[') && (str[str.length() - 1] == ']'))
		str = str.substr(1, str.length() - 2);
	else
		return a;

	
	while (str.length() > 0)
	{
		std::string tmp;

		
		size_t i = 0;
		for (; i < str.length(); i++)
		{
			if ((str[i] == '{') || (str[i] == '['))
			{
				Value v = DeserializeValue(str);
				if (v.GetType() != NULLVal)
					a.push_back(v);

				break;
			}

			bool terminate_parsing = false;

			if ((str[i] == ',') || (str[i] == ']'))
				terminate_parsing = true;
			else
			{
				tmp += str[i];
				if  (i == str.length() - 1)
					terminate_parsing = true;
			}

			if (terminate_parsing)
			{
				Value v = DeserializeValue(tmp);
				if (v.GetType() != NULLVal)
					a.push_back(v);

				str = str.substr(i + 1, str.length());
				break;
			}
		}
	}

	return a;
}

Object json::Deserialize(const std::string& _str)
{
	Object obj;

	std::string str = Trim(_str);

	//assert((str[0] == '{') && (str[str.length() - 1] == '}'));
	if ((str[0] == '{') && (str[str.length() - 1] == '}'))
		str = str.substr(1, str.length() - 2);
	else
		return obj;

	while (str.length() > 0)
	{
		// Get the key name
		size_t start_quote_idx = str.find('\"');
		size_t end_quote_idx = str.find('\"', start_quote_idx + 1);
		size_t colon_idx = str.find(':', end_quote_idx);

		//assert((start_quote_idx != std::string::npos) && (end_quote_idx != std::string::npos) && (colon_idx != std::string::npos));
		std::string key = str.substr(start_quote_idx + 1, end_quote_idx - start_quote_idx - 1);
		//assert(key.length() > 0);
		if (key.length() == 0)
			return Object();

		str = str.substr(colon_idx + 1, str.length());
		obj[key] = DeserializeValue(str);
	}

	return obj;
}