/*
 * Copyright (C) 2010 The Android Open Source Project
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

#pragma once

/**
 * Native input manager.
 */

#include "InputDeviceMetricsCollector.h"
#include "InputFilter.h"
#include "InputProcessor.h"
#include "InputReaderBase.h"
#include "PointerChoreographer.h"
#include "include/UnwantedInteractionBlockerInterface.h"

#include <InputDispatcherInterface.h>
#include <InputDispatcherPolicyInterface.h>
#include <InputFilterPolicyInterface.h>
#include <PointerChoreographerPolicyInterface.h>
#include <input/Input.h>
#include <input/InputTransport.h>

#include <aidl/com/android/server/inputflinger/IInputFlingerRust.h>
#include <android/os/BnInputFlinger.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/Timers.h>

using android::os::BnInputFlinger;

using aidl::com::android::server::inputflinger::IInputFilter;
using aidl::com::android::server::inputflinger::IInputFlingerRust;

namespace android {
class InputChannel;
class InputDispatcherThread;

/*
 * The input manager is the core of the system event processing.
 *
 * The input manager has three components.
 *
 * 1. The InputReader class starts a thread that reads and preprocesses raw input events, applies
 *    policy, and posts messages to a queue managed by the UnwantedInteractionBlocker.
 * 2. The UnwantedInteractionBlocker is responsible for removing unwanted interactions. For example,
 *    this could be a palm on the screen. This stage would alter the event stream to remove either
 *    partially (some of the pointers) or fully (all touches) the unwanted interaction. The events
 *    are processed on the InputReader thread, without any additional queue. The events are then
 *    posted to the queue managed by the InputProcessor.
 * 3. The InputProcessor class starts a thread to communicate with the device-specific
 *    IInputProcessor HAL. It then waits on the queue of events from UnwantedInteractionBlocker,
 *    processes the events (for example, applies a classification to the events), and queues them
 *    for the InputDispatcher.
 * 4. The InputDispatcher class starts a thread that waits for new events on the
 *    previous queue and asynchronously dispatches them to applications.
 *
 * By design, none of these classes share any internal state.  Moreover, all communication is
 * done one way from the InputReader to the InputDispatcher and never the reverse.  All
 * classes may interact with the InputDispatchPolicy, however.
 *
 * The InputManager class never makes any calls into Java itself.  Instead, the
 * InputDispatchPolicy is responsible for performing all external interactions with the
 * system, including calling DVM services.
 */
class InputManagerInterface : public virtual RefBase {
protected:
    InputManagerInterface() { }
    virtual ~InputManagerInterface() { }

public:
    /* Starts the input threads. */
    virtual status_t start() = 0;

    /* Stops the input threads and waits for them to exit. */
    virtual status_t stop() = 0;

    /* Gets the input reader. */
    virtual InputReaderInterface& getReader() = 0;

    /* Gets the PointerChoreographer. */
    virtual PointerChoreographerInterface& getChoreographer() = 0;

    /* Gets the input processor. */
    virtual InputProcessorInterface& getProcessor() = 0;

    /* Gets the metrics collector. */
    virtual InputDeviceMetricsCollectorInterface& getMetricsCollector() = 0;

    /* Gets the input dispatcher. */
    virtual InputDispatcherInterface& getDispatcher() = 0;

    /* Gets the input filter */
    virtual InputFilterInterface& getInputFilter() = 0;

    /* Check that the input stages have not deadlocked. */
    virtual void monitor() = 0;

    /* Dump the state of the components controlled by the input manager. */
    virtual void dump(std::string& dump) = 0;
};

class InputManager : public InputManagerInterface, public BnInputFlinger {
protected:
    ~InputManager() override;

public:
    InputManager(const sp<InputReaderPolicyInterface>& readerPolicy,
                 InputDispatcherPolicyInterface& dispatcherPolicy,
                 PointerChoreographerPolicyInterface& choreographerPolicy,
                 InputFilterPolicyInterface& inputFilterPolicy);

    status_t start() override;
    status_t stop() override;

    InputReaderInterface& getReader() override;
    PointerChoreographerInterface& getChoreographer() override;
    InputProcessorInterface& getProcessor() override;
    InputDeviceMetricsCollectorInterface& getMetricsCollector() override;
    InputDispatcherInterface& getDispatcher() override;
    InputFilterInterface& getInputFilter() override;
    void monitor() override;
    void dump(std::string& dump) override;

    status_t dump(int fd, const Vector<String16>& args) override;
    binder::Status createInputChannel(const std::string& name,
                                      android::os::InputChannelCore* outChannel) override;
    binder::Status removeInputChannel(const sp<IBinder>& connectionToken) override;
    binder::Status setFocusedWindow(const gui::FocusRequest&) override;

private:
    std::unique_ptr<InputReaderInterface> mReader;

    std::unique_ptr<UnwantedInteractionBlockerInterface> mBlocker;

    std::unique_ptr<InputFilterInterface> mInputFilter;

    std::unique_ptr<PointerChoreographerInterface> mChoreographer;

    std::unique_ptr<InputProcessorInterface> mProcessor;

    std::unique_ptr<InputDeviceMetricsCollectorInterface> mCollector;

    std::unique_ptr<InputDispatcherInterface> mDispatcher;

    std::shared_ptr<IInputFlingerRust> mInputFlingerRust;

    std::vector<std::unique_ptr<TracedInputListener>> mTracingStages;
};

} // namespace android
