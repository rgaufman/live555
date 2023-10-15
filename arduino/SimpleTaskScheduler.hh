#pragma once
#include "BasicUsageEnvironment.hh"
/**
 * @brief SimpleTaskScheduler for Arduino which supports SingleStep calls from the loop
 * @author Phil Schatzmann
 * 
*/
class SimpleTaskScheduler : public BasicTaskScheduler {
  public:
  static SimpleTaskScheduler* createNew(unsigned maxSchedulerGranularity = 10000/*microseconds*/){
    return new SimpleTaskScheduler(maxSchedulerGranularity);
  }
  void SingleStep(unsigned int maxDelayTime = 0) override {
    return BasicTaskScheduler::SingleStep(maxDelayTime);
  }

protected:
  SimpleTaskScheduler(unsigned maxSchedulerGranularity) : BasicTaskScheduler(maxSchedulerGranularity){}
};

