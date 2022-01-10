
#ifndef __transport_def_h__
#define __transport_def_h__

#ifdef _WIN32
#pragma warning( disable : 4786 4146 4244 4018 4663 4100 4710 4511 4512)
#pragma warning(push, 3)
#endif

#ifdef _WIN32
//------------WINDOWS--------------------------------
	#include <Winsock2.h>
	#include <windows.h>
	#pragma  comment(lib,"Ws2_32.lib")
#elif defined(__linux__) || defined(__sgi) || defined(__FreeBSD__) || defined(__sparc)
//------------LINUX,SGI,FREEBSD,SPARC--------------------------------
	#include <sys/socket.h>
	#include <sys/select.h>
	#include <sys/ioctl.h>
	#include <sys/uio.h>
	#include <sys/time.h>
#ifdef RT_LINUX
	#include <netinet/ip.h>
#endif
	#include <netinet/in.h>
	#include <arpa/inet.h>
	#include <unistd.h>
	#include <netdb.h>
#ifdef RT_LINUX
	#include <execinfo.h>
#endif
	#include <pwd.h>
	#include <stdarg.h>
	
	#if defined(__linux__)
		#include <endian.h>
	#elif defined(__sgi)
		#include <sys/endian.h>
	#elif defined(__sparc)
		#include <sys/isa_defs.h>
		#define __LITTLE_ENDIAN 1234
		#define __BIG_ENDIAN 4321
		#if defined(_LITTLE_ENDIAN)
			#define __BYTE_ORDER __LITTLE_ENIDAN
		#elif defined(_BIG_ENDIAN)
			#define __BYTE_ORDER __BIG_ENDIAN 
		#endif
	#elif defined(__FreeBSD__)
		#include <machine/endian.h>
	#endif

	typedef struct sockaddr_in SOCKADDR_IN ;
	typedef struct sockaddr    SOCKADDR ;
	typedef struct sockaddr_storage  SOCKADDR_STORAGE;
	#define SOCKET			int
	#define INVALID_SOCKET (int)(-1)
	#define SOCKET_ERROR   (int)(-1)
	#define closesocket	   close
	#define ioctlsocket    ioctl
	#define stricmp		   strcasecmp
#elif defined(__vxworks)
//------------VXWORKS--------------------------------
	#include <netinet/in.h>
	#include <netinet/in_systm.h>
	#include <selectLib.h>
	#define __BYTE_ORDER _BYTE_ORDER
	#define __LITTLE_ENDIAN _LITTLE_ENDIAN
	#define __BIG_ENDIAN _BIG_ENDIAN
	//....
#else
//------------OTHER--------------------------------
#endif


//------------COMMON DEFINES-----------------------
#ifdef MACOS
#include <string>
#else

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#ifndef RT_SOLARIS
#include <string.h>
#include <string>
#endif
#include <assert.h>
#include <cassert>
#include <limits.h>
#include <ctype.h>
#include <math.h>
#include <errno.h>
#include <fstream>
#include <fcntl.h>
#endif

#include <exception>
#include <iostream>
#include <map>
#include <list>
#include <vector>
#include <algorithm>

#ifndef RT_SOLARIS
using namespace std;
#endif


#define min2(a,b)	((a)>(b)?(b):(a))
#define max2(a,b)	((a)>(b)?(a):(b))
#define min3(a,b,c) (min2(min2(a,b),c))
#define max3(a,b,c) (max2(max2(a,b),c))

#define RTP_min2(a,b)	((a)>(b)?(b):(a))
#define RTP_max2(a,b)	((a)>(b)?(a):(b))
#define RTP_min3(a,b,c) (RTP_min2(RTP_min2(a,b),c))
#define RTP_max3(a,b,c) (RTP_max2(RTP_max2(a,b),c))
#define RTP_abs(a)		((a)>0?(a):-(a))
#ifdef _WIN32
#pragma warning(pop)
#endif

#endif //#ifndef _transport_def_h_
