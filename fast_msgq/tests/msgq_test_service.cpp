/*
* Copyright (C) 2016 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*      http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include <../common/MessageQueue.h>
#include <android/hardware/tests/msgq/1.0/ITestMsgQ.h>
#include <cutils/ashmem.h>
#include <hidl/IServiceManager.h>
#include <hwbinder/IInterface.h>
#include <hwbinder/IPCThreadState.h>
#include <hwbinder/ProcessState.h>
#include <utils/Looper.h>
#include <utils/StrongPointer.h>

// libutils:
using android::Looper;
using android::LooperCallback;
using android::OK;
using android::sp;

// libhwbinder:
using android::hardware::BnInterface;
using android::hardware::defaultServiceManager;
using android::hardware::IInterface;
using android::hardware::IPCThreadState;
using android::hardware::Parcel;
using android::hardware::ProcessState;
using android::hardware::Return;
using android::hardware::Void;

// Standard library
using std::string;
using std::vector;

// libhidl
using android::hardware::kSynchronizedReadWrite;
using android::hardware::MQDescriptorSync;
using android::hardware::MessageQueue;

// Generated HIDL files
using android::hardware::tests::msgq::V1_0::ITestMsgQ;

const char kServiceName[] = "android.hardware.tests.msgq@1.0::ITestMsgQ";

namespace {

class BinderCallback : public LooperCallback {
 public:
  BinderCallback() {}
  ~BinderCallback() override {}

  int handleEvent(int /* fd */, int /* events */, void* /* data */) override {
    IPCThreadState::self()->handlePolledCommands();
    return 1;  // Continue receiving callbacks.
  }
};

class TestMsgQ : public ITestMsgQ {
 public:
  TestMsgQ() : fmsg_queue(nullptr) {}
  virtual ~TestMsgQ() {
    if (fmsg_queue) {
      delete fmsg_queue;
    }
  }

  // TODO:: Change callback argument to bool.
  virtual Return<int32_t> requestWrite(int count) {
    uint16_t* data = new uint16_t[count];
    for (int i = 0; i < count; i++) {
      data[i] = i;
    }
    if (fmsg_queue->write(data, count)) {
      delete[] data;
      return count;
    }
    delete[] data;
    return 0;
  }
  // TODO:: Change callback argument to bool.
  virtual Return<int32_t> requestRead(int count) {
    uint16_t* data = new uint16_t[count];
    if (fmsg_queue->read(data, count) && verifyData(data, count)) {
      delete[] data;
      return count;
    }
    delete[] data;
    return 0;
  }

  virtual Return<void> configureFmqSyncReadWrite(
      ITestMsgQ::configureFmqSyncReadWrite_cb callback) {
    size_t eventQueueTotal = 4096;
    const size_t eventQueueDataSize = 2048;
    int ashmemFd = ashmem_create_region("MessageQueue", eventQueueTotal);
    ashmem_set_prot_region(ashmemFd, PROT_READ | PROT_WRITE);
    /*
     * The native handle will contain the fds to be mapped.
     */
    native_handle_t* mq_handle =
        native_handle_create(1 /* numFds */, 0 /* numInts */);
    if (!mq_handle) {
      ALOGE("Unable to create native_handle_t");
      callback(-1 /* ret */, MQDescriptorSync
               (std::vector<android::hardware::GrantorDescriptor>(),
                       nullptr /* nhandle */, 0 /* size */));
      return Void();
    }

    mq_handle->data[0] = ashmemFd;

    MQDescriptorSync mydesc(eventQueueDataSize, mq_handle,
                                     sizeof(uint16_t));
    if (fmsg_queue) {
      delete fmsg_queue;
    }
    fmsg_queue = new MessageQueue<uint16_t, kSynchronizedReadWrite>(mydesc);
    if (fmsg_queue == nullptr) {
      callback(-1 /* ret */, MQDescriptorSync(
                   std::vector<android::hardware::GrantorDescriptor>(),
                   nullptr /* nhandle */, 0 /* size */));
    } else {
      callback(0 /* ret */, mydesc);
    }
    return Void();
  }
  android::hardware::MessageQueue<uint16_t,
      android::hardware::kSynchronizedReadWrite>* fmsg_queue;

 private:
  /*
   * Utility function to verify data read from the fast message queue.
   */
  bool verifyData(uint16_t* data, int count) {
    for (int i = 0; i < count; i++) {
      if (data[i] != i) return false;
    }

    return true;
  }
};

int Run() {
  android::sp<TestMsgQ> service = new TestMsgQ;
  sp<Looper> looper(Looper::prepare(0 /* opts */));

  int binder_fd = -1;
  ProcessState::self()->setThreadPoolMaxThreadCount(0);
  IPCThreadState::self()->disableBackgroundScheduling(true);
  IPCThreadState::self()->setupPolling(&binder_fd);
  if (binder_fd < 0) return -1;

  sp<BinderCallback> cb(new BinderCallback);
  if (looper->addFd(binder_fd, Looper::POLL_CALLBACK, Looper::EVENT_INPUT, cb,
                    nullptr) != 1) {
    ALOGE("Failed to add binder FD to Looper");
    return -1;
  }
  service->registerAsService(kServiceName);

  ALOGI("Entering loop");
  while (true) {
    const int result = looper->pollAll(-1 /* timeoutMillis */);
    ALOGI("Looper returned %d", result);
  }
  return 0;
}

}  // namespace

int main(int /* argc */, char* /* argv */ []) { return Run(); }