//
// Copyright (c) 2021- anjisuan783
//
// SPDX-License-Identifier: MIT
//

#ifndef __MEDIA_NETWORK_H__
#define __MEDIA_NETWORK_H__

#include "common/media_kernel_error.h"
#include "common/media_define.h"
#include "media_msg_queue.h"

namespace ma {

class MediaHandler {
public:
	typedef long MASK;
	enum {
		NULL_MASK = 0,
		ACCEPT_MASK = (1 << 0),
		CONNECT_MASK = (1 << 1),
		READ_MASK = (1 << 2),
		WRITE_MASK = (1 << 3),
		EXCEPT_MASK = (1 << 4),
		TIMER_MASK = (1 << 5),
		ALL_EVENTS_MASK = READ_MASK |
                      WRITE_MASK |
                      EXCEPT_MASK |
                      ACCEPT_MASK |
                      CONNECT_MASK |
                      TIMER_MASK,
		SHOULD_CALL = (1 << 6),
		CLOSE_MASK = (1 << 7),
		EVENTQUEUE_MASK = (1 << 8)
	};

	static std::string GetMaskString(MASK);

	virtual MEDIA_HANDLE GetHandle() const = 0;
	
	/// Called when input events occur (e.g., data is ready).
	/// OnClose() will be callbacked if return -1.
	virtual int OnInput(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE);

	/// Called when output events are possible (e.g., when flow control
	/// abates or non-blocking connection completes).
	/// OnClose() will be callbacked if return -1.
	virtual int OnOutput(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE);

	/// Called when an exceptional events occur (e.g., OOB data).
	/// OnClose() will be callbacked if return -1.
	/// Not implemented yet.
	virtual int OnException(MEDIA_HANDLE aFd = MEDIA_INVALID_HANDLE);

	/**
	 * Called when a <On*()> method returns -1 or when the
	 * <RemoveHandler> method is called on an <CReactor>.  The
	 * <aMask> indicates which event has triggered the
	 * <HandleClose> method callback on a particular <aFd>.
	 */
	virtual int OnClose(MEDIA_HANDLE aFd, MASK aMask);
	
	virtual ~MediaHandler() = default;
};

class MediaReactor : public MediaMsgQueue, public MediaTimerQueue {
 public:
	// Initialization.
	virtual srs_error_t Open() = 0;

	/**
	 * Register <aEh> with <aMask>.  The handle will always
	 * come from <GetHandle> on the <aEh>.
	 * If success:
	 *    if <aEh> is registered, return ERROR_SYSTEM_EXISTED;
	 *    else return srs_success;
	 */
	virtual srs_error_t RegisterHandler(
		MediaHandler *aEh,
		MediaHandler::MASK aMask) = 0;

	/**
	 * Removes <aEh> according to <aMask>. 
	 * If success:
	 *    if <aEh> is registered
	 *       If <aMask> equals or greater than that registered, return srs_success;
	 *       else return ERROR_SYSTEM_EXISTED;
	 *    else return ERROR_SYSTEM_EXISTED;
	 */
	virtual srs_error_t RemoveHandler(
		MediaHandler *aEh,
		MediaHandler::MASK aMask = MediaHandler::ALL_EVENTS_MASK) = 0;

	virtual srs_error_t NotifyHandler(
		MediaHandler *aEh,
		MediaHandler::MASK aMask) = 0;

	virtual srs_error_t RunEventLoop() = 0;

	/// this function can be invoked in the different thread.
	virtual void StopEventLoop() = 0;

	/// Close down and release all resources.
	virtual srs_error_t Close() = 0;

	virtual ~MediaReactor() = default;
};

} //namespace ma

#endif //!__MEDIA_NETWORK_H__
