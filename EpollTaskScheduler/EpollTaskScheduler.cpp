/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// Copyright (c) 1996-2016 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#include "EpollTaskScheduler.hh"
#if defined(__WIN32__) || defined(_WIN32)
#include "wepoll.h"
#define EPOLL_INVALID NULL
#else
#include <sys/epoll.h>
#define EPOLL_INVALID -1
#define epoll_close close
#endif

////////// EpollTaskScheduler //////////

EpollTaskScheduler* EpollTaskScheduler::createNew(unsigned maxSchedulerGranularity) {
  return new EpollTaskScheduler(maxSchedulerGranularity);
}

EpollTaskScheduler::EpollTaskScheduler(unsigned maxSchedulerGranularity)
  : fMaxSchedulerGranularity(maxSchedulerGranularity), fEpollHandle(EPOLL_INVALID)
{
  fEpollHandle = epoll_create(1024 /*ignored*/);

  if (fEpollHandle == EPOLL_INVALID) {
    internalError();
  }

  if (maxSchedulerGranularity > 0) schedulerTickTask(); // ensures that we handle events frequently
}

EpollTaskScheduler::~EpollTaskScheduler() {
  if (fEpollHandle != EPOLL_INVALID) {
    epoll_close(fEpollHandle);
  }
}

void EpollTaskScheduler::schedulerTickTask(void* clientData) {
  ((EpollTaskScheduler*)clientData)->schedulerTickTask();
}

void EpollTaskScheduler::schedulerTickTask() {
  scheduleDelayedTask(fMaxSchedulerGranularity, schedulerTickTask, this);
}

#ifndef MILLION
#define MILLION 1000000
#endif

void EpollTaskScheduler::SingleStep(unsigned maxDelayTime) {
  DelayInterval const& timeToDelay = fDelayQueue.timeToNextAlarm();
  struct timeval tv_timeToDelay;
  tv_timeToDelay.tv_sec = timeToDelay.seconds();
  tv_timeToDelay.tv_usec = timeToDelay.useconds();
  // Very large "tv_sec" values cause select() to fail.
  // Don't make it any larger than 1 million seconds (11.5 days)
  const long MAX_TV_SEC = MILLION;
  if (tv_timeToDelay.tv_sec > MAX_TV_SEC) {
    tv_timeToDelay.tv_sec = MAX_TV_SEC;
  }
  // Also check our "maxDelayTime" parameter (if it's > 0):
  if (maxDelayTime > 0 &&
      (tv_timeToDelay.tv_sec > (long)maxDelayTime/MILLION ||
       (tv_timeToDelay.tv_sec == (long)maxDelayTime/MILLION &&
    tv_timeToDelay.tv_usec > (long)maxDelayTime%MILLION))) {
    tv_timeToDelay.tv_sec = maxDelayTime/MILLION;
    tv_timeToDelay.tv_usec = maxDelayTime%MILLION;
  }

  const int timeout = tv_timeToDelay.tv_sec * 1000 + tv_timeToDelay.tv_usec / 1000;
  epoll_event event;
  int ret = epoll_wait(fEpollHandle, &event, 1, timeout);
  if (ret < 0) {
    if (errno != EINTR) {
      internalError();
    }
  }

  if (ret > 0) {
    const HandlerDescriptor* handler =
      static_cast<const HandlerDescriptor*>(event.data.ptr);
    if (handler != NULL) {
      int resultConditionSet = 0;
      if (event.events & EPOLLIN)  resultConditionSet |= SOCKET_READABLE;
      if (event.events & EPOLLOUT) resultConditionSet |= SOCKET_WRITABLE;
      if (event.events & EPOLLERR) resultConditionSet |= SOCKET_EXCEPTION;

      if ((resultConditionSet & handler->conditionSet) != 0) {
        (*handler->handlerProc)(handler->clientData, resultConditionSet);
      }
    }
  }

  // Also handle any newly-triggered event (Note that we do this *after* calling a socket handler,
  // in case the triggered event handler modifies The set of readable sockets.)
  if (fTriggersAwaitingHandling != 0) {
    if (fTriggersAwaitingHandling == fLastUsedTriggerMask) {
      // Common-case optimization for a single event trigger:
      fTriggersAwaitingHandling &=~ fLastUsedTriggerMask;
      if (fTriggeredEventHandlers[fLastUsedTriggerNum] != NULL) {
    (*fTriggeredEventHandlers[fLastUsedTriggerNum])(fTriggeredEventClientDatas[fLastUsedTriggerNum]);
      }
    } else {
      // Look for an event trigger that needs handling (making sure that we make forward progress through all possible triggers):
      unsigned i = fLastUsedTriggerNum;
      EventTriggerId mask = fLastUsedTriggerMask;

      do {
    i = (i+1)%MAX_NUM_EVENT_TRIGGERS;
    mask >>= 1;
    if (mask == 0) mask = 0x80000000;

    if ((fTriggersAwaitingHandling&mask) != 0) {
      fTriggersAwaitingHandling &=~ mask;
      if (fTriggeredEventHandlers[i] != NULL) {
        (*fTriggeredEventHandlers[i])(fTriggeredEventClientDatas[i]);
      }

      fLastUsedTriggerMask = mask;
      fLastUsedTriggerNum = i;
      break;
    }
      } while (i != fLastUsedTriggerNum);
    }
  }

  // Also handle any delayed event that may have come due.
  fDelayQueue.handleAlarm();
}

void EpollTaskScheduler
  ::setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData) {
  if (socketNum < 0) return;

  epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  const HandlerDescriptor* handler = lookupHandlerDescriptor(socketNum);
  if (handler != NULL) {
      epoll_ctl(fEpollHandle, EPOLL_CTL_DEL, socketNum, &ev);
  }

  if (conditionSet == 0) {
    fHandlers->clearHandler(socketNum);
  } else {
    fHandlers->assignHandler(socketNum, conditionSet, handlerProc, clientData);

    handler = lookupHandlerDescriptor(socketNum);
    ev.data.ptr = const_cast<HandlerDescriptor*>(handler);

    if (conditionSet&SOCKET_READABLE) ev.events |= EPOLLIN;
    if (conditionSet&SOCKET_WRITABLE) ev.events |= EPOLLOUT;

    epoll_ctl(fEpollHandle, EPOLL_CTL_ADD, socketNum, &ev);
  }
}

void EpollTaskScheduler::moveSocketHandling(int oldSocketNum, int newSocketNum) {
  if (oldSocketNum < 0 || newSocketNum < 0) return; // sanity check

  const HandlerDescriptor* handler = lookupHandlerDescriptor(oldSocketNum);
  if (handler == NULL) {
    return;
  }

  epoll_event ev;
  memset(&ev, 0, sizeof(ev));

  if (handler->conditionSet&SOCKET_READABLE) ev.events |= EPOLLIN;
  if (handler->conditionSet&SOCKET_WRITABLE) ev.events |= EPOLLOUT;

  epoll_ctl(fEpollHandle, EPOLL_CTL_DEL, oldSocketNum, &ev);
  epoll_ctl(fEpollHandle, EPOLL_CTL_ADD, newSocketNum, &ev);

  fHandlers->moveHandler(oldSocketNum, newSocketNum);
}

const HandlerDescriptor* EpollTaskScheduler::lookupHandlerDescriptor(int socketNum) const {
  HandlerDescriptor* handler;
  HandlerIterator iter(*fHandlers);
  while ((handler = iter.next()) != NULL) {
    if (handler->socketNum == socketNum) break;
  }
  return handler;
}
