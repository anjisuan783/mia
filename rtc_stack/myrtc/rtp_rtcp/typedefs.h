#ifndef TYPEDEFS_H_07_06_2004_
#define TYPEDEFS_H_07_06_2004_

typedef int		_int32_t;
typedef short	_int16_t;
typedef char	_int8_t;
#ifdef _WIN32
typedef __int64	_int64_t;
#else
typedef int64_t	_int64_t;
#endif
typedef unsigned int	_uint32_t;
typedef unsigned short	_uint16_t;
typedef unsigned char	_uint8_t;
#ifdef _WIN32
typedef unsigned __int64	_uint64_t;
#else
typedef uint64_t			_uint64_t;
#endif

typedef float				_real32_t;
typedef double				_real64_t;
typedef long double			_real80_t;

typedef bool				_boolean_t;

#endif // #ifndef TYPEDEFS_H_07_06_2004_


