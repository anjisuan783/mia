#ifndef __MEDIA_SOCKET_H__
#define __MEDIA_SOCKET_H__

#include "common/media_define.h"

namespace ma {

class MEDIA_IPC_SAP {
public:
	enum { NON_BLOCK = 0 };

	MEDIA_IPC_SAP() : m_Handle(MEDIA_INVALID_HANDLE) { }

	MEDIA_HANDLE GetHandle() const;
	void SetHandle(MEDIA_HANDLE aNew);

	int Enable(int aValue) const ;
	int Disable(int aValue) const ;
	int Control(int aCmd, void *aArg) const;
	
protected:
	MEDIA_HANDLE m_Handle;
};

}

#endif //!__MEDIA_SOCKET_H__
