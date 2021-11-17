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
// C++ header

#ifndef _EPOLL_TASK_SCHEDULER_HH
#define _EPOLL_TASK_SCHEDULER_HH

#include "BasicUsageEnvironment.hh"
#include "HandlerSet.hh"

class EpollTaskScheduler : public BasicTaskScheduler0 {
public:
  static EpollTaskScheduler* createNew(unsigned maxSchedulerGranularity = 10000 /*microseconds*/);
  virtual ~EpollTaskScheduler();

private:
  EpollTaskScheduler(unsigned maxSchedulerGranularity);

  static void schedulerTickTask(void* clientData);
  void schedulerTickTask();

  const HandlerDescriptor* lookupHandlerDescriptor(int socketNum) const;

private:
  virtual void SingleStep(unsigned maxDelayTime);

  virtual void setBackgroundHandling(int socketNum, int conditionSet, BackgroundHandlerProc* handlerProc, void* clientData);
  virtual void moveSocketHandling(int oldSocketNum, int newSocketNum);

private:
  unsigned fMaxSchedulerGranularity;

private:
#if defined(__WIN32__) || defined(_WIN32)
  void* fEpollHandle;
#else
  int fEpollHandle;
#endif
};

#endif
