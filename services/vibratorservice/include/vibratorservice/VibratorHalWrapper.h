/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef ANDROID_OS_VIBRATORHALWRAPPER_H
#define ANDROID_OS_VIBRATORHALWRAPPER_H

#include <android-base/thread_annotations.h>
#include <android/hardware/vibrator/1.3/IVibrator.h>
#include <android/hardware/vibrator/BnVibratorCallback.h>
#include <android/hardware/vibrator/IVibrator.h>
#include <binder/IServiceManager.h>
#include <cutils/properties.h>

#include <vibratorservice/VibratorCallbackScheduler.h>

namespace android {

namespace vibrator {

// -------------------------------------------------------------------------------------------------

// Base class to represent a generic result of a call to the Vibrator HAL wrapper.
class BaseHalResult {
public:
    bool isOk() const { return mStatus == SUCCESS; }
    bool isFailed() const { return mStatus == FAILED; }
    bool isUnsupported() const { return mStatus == UNSUPPORTED; }
    bool shouldRetry() const { return isFailed() && mDeadObject; }
    const char* errorMessage() const { return mErrorMessage.c_str(); }

protected:
    enum Status { SUCCESS, UNSUPPORTED, FAILED };
    Status mStatus;
    std::string mErrorMessage;
    bool mDeadObject;

    explicit BaseHalResult(Status status, const char* errorMessage = "", bool deadObject = false)
          : mStatus(status), mErrorMessage(errorMessage), mDeadObject(deadObject) {}
    virtual ~BaseHalResult() = default;
};

// Result of a call to the Vibrator HAL wrapper, holding data if successful.
template <typename T>
class HalResult : public BaseHalResult {
public:
    static HalResult<T> ok(T value) { return HalResult(value); }
    static HalResult<T> unsupported() { return HalResult(Status::UNSUPPORTED); }
    static HalResult<T> failed(const char* msg) { return HalResult(Status::FAILED, msg); }
    static HalResult<T> transactionFailed(const char* msg) {
        return HalResult(Status::FAILED, msg, /* deadObject= */ true);
    }

    // This will throw std::bad_optional_access if this result is not ok.
    const T& value() const { return mValue.value(); }
    const T valueOr(T&& defaultValue) const { return mValue.value_or(defaultValue); }

private:
    std::optional<T> mValue;

    explicit HalResult(T value)
          : BaseHalResult(Status::SUCCESS), mValue(std::make_optional(value)) {}
    explicit HalResult(Status status, const char* errorMessage = "", bool deadObject = false)
          : BaseHalResult(status, errorMessage, deadObject), mValue() {}
};

// Empty result of a call to the Vibrator HAL wrapper.
template <>
class HalResult<void> : public BaseHalResult {
public:
    static HalResult<void> ok() { return HalResult(Status::SUCCESS); }
    static HalResult<void> unsupported() { return HalResult(Status::UNSUPPORTED); }
    static HalResult<void> failed(const char* msg) { return HalResult(Status::FAILED, msg); }
    static HalResult<void> transactionFailed(const char* msg) {
        return HalResult(Status::FAILED, msg, /* deadObject= */ true);
    }

private:
    explicit HalResult(Status status, const char* errorMessage = "", bool deadObject = false)
          : BaseHalResult(status, errorMessage, deadObject) {}
};

// -------------------------------------------------------------------------------------------------

// Factory functions that convert failed HIDL/AIDL results into HalResult instances.
// Implementation of static template functions needs to be in this header file for the linker.
class HalResultFactory {
public:
    template <typename T>
    static HalResult<T> fromStatus(binder::Status status, T data) {
        return status.isOk() ? HalResult<T>::ok(data) : fromFailedStatus<T>(status);
    }

    template <typename T>
    static HalResult<T> fromStatus(hardware::vibrator::V1_0::Status status, T data) {
        return (status == hardware::vibrator::V1_0::Status::OK) ? HalResult<T>::ok(data)
                                                                : fromFailedStatus<T>(status);
    }

    template <typename T, typename R>
    static HalResult<T> fromReturn(hardware::Return<R>& ret, T data) {
        return ret.isOk() ? HalResult<T>::ok(data) : fromFailedReturn<T, R>(ret);
    }

    template <typename T, typename R>
    static HalResult<T> fromReturn(hardware::Return<R>& ret,
                                   hardware::vibrator::V1_0::Status status, T data) {
        return ret.isOk() ? fromStatus<T>(status, data) : fromFailedReturn<T, R>(ret);
    }

    static HalResult<void> fromStatus(status_t status) {
        return (status == android::OK) ? HalResult<void>::ok() : fromFailedStatus<void>(status);
    }

    static HalResult<void> fromStatus(binder::Status status) {
        return status.isOk() ? HalResult<void>::ok() : fromFailedStatus<void>(status);
    }

    static HalResult<void> fromStatus(hardware::vibrator::V1_0::Status status) {
        return (status == hardware::vibrator::V1_0::Status::OK) ? HalResult<void>::ok()
                                                                : fromFailedStatus<void>(status);
    }

    template <typename R>
    static HalResult<void> fromReturn(hardware::Return<R>& ret) {
        return ret.isOk() ? HalResult<void>::ok() : fromFailedReturn<void, R>(ret);
    }

private:
    template <typename T>
    static HalResult<T> fromFailedStatus(status_t status) {
        auto msg = "status_t = " + statusToString(status);
        return (status == android::DEAD_OBJECT) ? HalResult<T>::transactionFailed(msg.c_str())
                                                : HalResult<T>::failed(msg.c_str());
    }

    template <typename T>
    static HalResult<T> fromFailedStatus(binder::Status status) {
        if (status.exceptionCode() == binder::Status::EX_UNSUPPORTED_OPERATION ||
            status.transactionError() == android::UNKNOWN_TRANSACTION) {
            // UNKNOWN_TRANSACTION means the HAL implementation is an older version, so this is
            // the same as the operation being unsupported by this HAL. Should not retry.
            return HalResult<T>::unsupported();
        }
        if (status.exceptionCode() == binder::Status::EX_TRANSACTION_FAILED) {
            return HalResult<T>::transactionFailed(status.toString8().c_str());
        }
        return HalResult<T>::failed(status.toString8().c_str());
    }

    template <typename T>
    static HalResult<T> fromFailedStatus(hardware::vibrator::V1_0::Status status) {
        switch (status) {
            case hardware::vibrator::V1_0::Status::UNSUPPORTED_OPERATION:
                return HalResult<T>::unsupported();
            default:
                auto msg = "android::hardware::vibrator::V1_0::Status = " + toString(status);
                return HalResult<T>::failed(msg.c_str());
        }
    }

    template <typename T, typename R>
    static HalResult<T> fromFailedReturn(hardware::Return<R>& ret) {
        return ret.isDeadObject() ? HalResult<T>::transactionFailed(ret.description().c_str())
                                  : HalResult<T>::failed(ret.description().c_str());
    }
};

// -------------------------------------------------------------------------------------------------

class HalCallbackWrapper : public hardware::vibrator::BnVibratorCallback {
public:
    HalCallbackWrapper(std::function<void()> completionCallback)
          : mCompletionCallback(completionCallback) {}

    binder::Status onComplete() override {
        mCompletionCallback();
        return binder::Status::ok();
    }

private:
    const std::function<void()> mCompletionCallback;
};

// -------------------------------------------------------------------------------------------------

// Vibrator HAL capabilities.
enum class Capabilities : int32_t {
    NONE = 0,
    ON_CALLBACK = hardware::vibrator::IVibrator::CAP_ON_CALLBACK,
    PERFORM_CALLBACK = hardware::vibrator::IVibrator::CAP_PERFORM_CALLBACK,
    AMPLITUDE_CONTROL = hardware::vibrator::IVibrator::CAP_AMPLITUDE_CONTROL,
    EXTERNAL_CONTROL = hardware::vibrator::IVibrator::CAP_EXTERNAL_CONTROL,
    EXTERNAL_AMPLITUDE_CONTROL = hardware::vibrator::IVibrator::CAP_EXTERNAL_AMPLITUDE_CONTROL,
    COMPOSE_EFFECTS = hardware::vibrator::IVibrator::CAP_COMPOSE_EFFECTS,
    COMPOSE_PWLE_EFFECTS = hardware::vibrator::IVibrator::CAP_COMPOSE_PWLE_EFFECTS,
    ALWAYS_ON_CONTROL = hardware::vibrator::IVibrator::CAP_ALWAYS_ON_CONTROL,
};

inline Capabilities operator|(Capabilities lhs, Capabilities rhs) {
    using underlying = typename std::underlying_type<Capabilities>::type;
    return static_cast<Capabilities>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

inline Capabilities& operator|=(Capabilities& lhs, Capabilities rhs) {
    return lhs = lhs | rhs;
}

inline Capabilities operator&(Capabilities lhs, Capabilities rhs) {
    using underlying = typename std::underlying_type<Capabilities>::type;
    return static_cast<Capabilities>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

inline Capabilities& operator&=(Capabilities& lhs, Capabilities rhs) {
    return lhs = lhs & rhs;
}

// -------------------------------------------------------------------------------------------------

class Info {
public:
    const HalResult<Capabilities> capabilities;
    const HalResult<std::vector<hardware::vibrator::Effect>> supportedEffects;
    const HalResult<std::vector<hardware::vibrator::Braking>> supportedBraking;
    const HalResult<std::vector<hardware::vibrator::CompositePrimitive>> supportedPrimitives;
    const HalResult<std::vector<std::chrono::milliseconds>> primitiveDurations;
    const HalResult<std::chrono::milliseconds> primitiveDelayMax;
    const HalResult<std::chrono::milliseconds> pwlePrimitiveDurationMax;
    const HalResult<int32_t> compositionSizeMax;
    const HalResult<int32_t> pwleSizeMax;
    const HalResult<float> minFrequency;
    const HalResult<float> resonantFrequency;
    const HalResult<float> frequencyResolution;
    const HalResult<float> qFactor;
    const HalResult<std::vector<float>> maxAmplitudes;

    void logFailures() const {
        logFailure<Capabilities>(capabilities, "getCapabilities");
        logFailure<std::vector<hardware::vibrator::Effect>>(supportedEffects,
                                                            "getSupportedEffects");
        logFailure<std::vector<hardware::vibrator::Braking>>(supportedBraking,
                                                             "getSupportedBraking");
        logFailure<std::vector<hardware::vibrator::CompositePrimitive>>(supportedPrimitives,
                                                                        "getSupportedPrimitives");
        logFailure<std::vector<std::chrono::milliseconds>>(primitiveDurations,
                                                           "getPrimitiveDuration");
        logFailure<std::chrono::milliseconds>(primitiveDelayMax, "getPrimitiveDelayMax");
        logFailure<std::chrono::milliseconds>(pwlePrimitiveDurationMax,
                                              "getPwlePrimitiveDurationMax");
        logFailure<int32_t>(compositionSizeMax, "getCompositionSizeMax");
        logFailure<int32_t>(pwleSizeMax, "getPwleSizeMax");
        logFailure<float>(minFrequency, "getMinFrequency");
        logFailure<float>(resonantFrequency, "getResonantFrequency");
        logFailure<float>(frequencyResolution, "getFrequencyResolution");
        logFailure<float>(qFactor, "getQFactor");
        logFailure<std::vector<float>>(maxAmplitudes, "getMaxAmplitudes");
    }

    bool shouldRetry() const {
        return capabilities.shouldRetry() || supportedEffects.shouldRetry() ||
                supportedBraking.shouldRetry() || supportedPrimitives.shouldRetry() ||
                primitiveDurations.shouldRetry() || primitiveDelayMax.shouldRetry() ||
                pwlePrimitiveDurationMax.shouldRetry() || compositionSizeMax.shouldRetry() ||
                pwleSizeMax.shouldRetry() || minFrequency.shouldRetry() ||
                resonantFrequency.shouldRetry() || frequencyResolution.shouldRetry() ||
                qFactor.shouldRetry() || maxAmplitudes.shouldRetry();
    }

private:
    template <typename T>
    void logFailure(HalResult<T> result, const char* functionName) const {
        if (result.isFailed()) {
            ALOGE("Vibrator HAL %s failed: %s", functionName, result.errorMessage());
        }
    }
};

class InfoCache {
public:
    Info get() {
        return {mCapabilities,
                mSupportedEffects,
                mSupportedBraking,
                mSupportedPrimitives,
                mPrimitiveDurations,
                mPrimitiveDelayMax,
                mPwlePrimitiveDurationMax,
                mCompositionSizeMax,
                mPwleSizeMax,
                mMinFrequency,
                mResonantFrequency,
                mFrequencyResolution,
                mQFactor,
                mMaxAmplitudes};
    }

private:
    // Create a transaction failed results as default so we can retry on the first time we get them.
    static const constexpr char* MSG = "never loaded";
    HalResult<Capabilities> mCapabilities = HalResult<Capabilities>::transactionFailed(MSG);
    HalResult<std::vector<hardware::vibrator::Effect>> mSupportedEffects =
            HalResult<std::vector<hardware::vibrator::Effect>>::transactionFailed(MSG);
    HalResult<std::vector<hardware::vibrator::Braking>> mSupportedBraking =
            HalResult<std::vector<hardware::vibrator::Braking>>::transactionFailed(MSG);
    HalResult<std::vector<hardware::vibrator::CompositePrimitive>> mSupportedPrimitives =
            HalResult<std::vector<hardware::vibrator::CompositePrimitive>>::transactionFailed(MSG);
    HalResult<std::vector<std::chrono::milliseconds>> mPrimitiveDurations =
            HalResult<std::vector<std::chrono::milliseconds>>::transactionFailed(MSG);
    HalResult<std::chrono::milliseconds> mPrimitiveDelayMax =
            HalResult<std::chrono::milliseconds>::transactionFailed(MSG);
    HalResult<std::chrono::milliseconds> mPwlePrimitiveDurationMax =
            HalResult<std::chrono::milliseconds>::transactionFailed(MSG);
    HalResult<int32_t> mCompositionSizeMax = HalResult<int>::transactionFailed(MSG);
    HalResult<int32_t> mPwleSizeMax = HalResult<int>::transactionFailed(MSG);
    HalResult<float> mMinFrequency = HalResult<float>::transactionFailed(MSG);
    HalResult<float> mResonantFrequency = HalResult<float>::transactionFailed(MSG);
    HalResult<float> mFrequencyResolution = HalResult<float>::transactionFailed(MSG);
    HalResult<float> mQFactor = HalResult<float>::transactionFailed(MSG);
    HalResult<std::vector<float>> mMaxAmplitudes =
            HalResult<std::vector<float>>::transactionFailed(MSG);

    friend class HalWrapper;
};

// Wrapper for Vibrator HAL handlers.
class HalWrapper {
public:
    explicit HalWrapper(std::shared_ptr<CallbackScheduler> scheduler)
          : mCallbackScheduler(std::move(scheduler)) {}
    virtual ~HalWrapper() = default;

    /* reloads wrapped HAL service instance without waiting. This can be used to reconnect when the
     * service restarts, to rapidly retry after a failure.
     */
    virtual void tryReconnect() = 0;

    Info getInfo();

    virtual HalResult<void> ping() = 0;
    virtual HalResult<void> on(std::chrono::milliseconds timeout,
                               const std::function<void()>& completionCallback) = 0;
    virtual HalResult<void> off() = 0;

    virtual HalResult<void> setAmplitude(float amplitude) = 0;
    virtual HalResult<void> setExternalControl(bool enabled) = 0;

    virtual HalResult<void> alwaysOnEnable(int32_t id, hardware::vibrator::Effect effect,
                                           hardware::vibrator::EffectStrength strength) = 0;
    virtual HalResult<void> alwaysOnDisable(int32_t id) = 0;

    virtual HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) = 0;

    virtual HalResult<std::chrono::milliseconds> performComposedEffect(
            const std::vector<hardware::vibrator::CompositeEffect>& primitives,
            const std::function<void()>& completionCallback);

    virtual HalResult<void> performPwleEffect(
            const std::vector<hardware::vibrator::PrimitivePwle>& primitives,
            const std::function<void()>& completionCallback);

protected:
    // Shared pointer to allow CallbackScheduler to outlive this wrapper.
    const std::shared_ptr<CallbackScheduler> mCallbackScheduler;

    // Load and cache vibrator info, returning cached result is present.
    HalResult<Capabilities> getCapabilities();
    HalResult<std::vector<std::chrono::milliseconds>> getPrimitiveDurations();

    // Request vibrator info to HAL skipping cache.
    virtual HalResult<Capabilities> getCapabilitiesInternal() = 0;
    virtual HalResult<std::vector<hardware::vibrator::Effect>> getSupportedEffectsInternal();
    virtual HalResult<std::vector<hardware::vibrator::Braking>> getSupportedBrakingInternal();
    virtual HalResult<std::vector<hardware::vibrator::CompositePrimitive>>
    getSupportedPrimitivesInternal();
    virtual HalResult<std::vector<std::chrono::milliseconds>> getPrimitiveDurationsInternal(
            const std::vector<hardware::vibrator::CompositePrimitive>& supportedPrimitives);
    virtual HalResult<std::chrono::milliseconds> getPrimitiveDelayMaxInternal();
    virtual HalResult<std::chrono::milliseconds> getPrimitiveDurationMaxInternal();
    virtual HalResult<int32_t> getCompositionSizeMaxInternal();
    virtual HalResult<int32_t> getPwleSizeMaxInternal();
    virtual HalResult<float> getMinFrequencyInternal();
    virtual HalResult<float> getResonantFrequencyInternal();
    virtual HalResult<float> getFrequencyResolutionInternal();
    virtual HalResult<float> getQFactorInternal();
    virtual HalResult<std::vector<float>> getMaxAmplitudesInternal();

private:
    std::mutex mInfoMutex;
    InfoCache mInfoCache GUARDED_BY(mInfoMutex);
};

// Wrapper for the AIDL Vibrator HAL.
class AidlHalWrapper : public HalWrapper {
public:
    AidlHalWrapper(
            std::shared_ptr<CallbackScheduler> scheduler, sp<hardware::vibrator::IVibrator> handle,
            std::function<HalResult<sp<hardware::vibrator::IVibrator>>()> reconnectFn =
                    []() {
                        char propStr[PROP_VALUE_MAX] = {0};
                        property_get("sys.haptic.motor", propStr, "");
                        if (!strcmp(propStr, "linear") || !strcmp(propStr, "zlinear")){
                            return HalResult<sp<hardware::vibrator::IVibrator>>::ok(
                                    checkVintfService<hardware::vibrator::IVibrator>(String16("vibratorfeature")));
                        } else{
                            return HalResult<sp<hardware::vibrator::IVibrator>>::ok(
                                    checkVintfService<hardware::vibrator::IVibrator>());
                        }
                    })
          : HalWrapper(std::move(scheduler)),
            mReconnectFn(reconnectFn),
            mHandle(std::move(handle)) {}
    virtual ~AidlHalWrapper() = default;

    HalResult<void> ping() override final;
    void tryReconnect() override final;

    HalResult<void> on(std::chrono::milliseconds timeout,
                       const std::function<void()>& completionCallback) override final;
    HalResult<void> off() override final;

    HalResult<void> setAmplitude(float amplitude) override final;
    HalResult<void> setExternalControl(bool enabled) override final;

    HalResult<void> alwaysOnEnable(int32_t id, hardware::vibrator::Effect effect,
                                   hardware::vibrator::EffectStrength strength) override final;
    HalResult<void> alwaysOnDisable(int32_t id) override final;

    HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) override final;

    HalResult<std::chrono::milliseconds> performComposedEffect(
            const std::vector<hardware::vibrator::CompositeEffect>& primitives,
            const std::function<void()>& completionCallback) override final;

    HalResult<void> performPwleEffect(
            const std::vector<hardware::vibrator::PrimitivePwle>& primitives,
            const std::function<void()>& completionCallback) override final;

protected:
    HalResult<Capabilities> getCapabilitiesInternal() override final;
    HalResult<std::vector<hardware::vibrator::Effect>> getSupportedEffectsInternal() override final;
    HalResult<std::vector<hardware::vibrator::Braking>> getSupportedBrakingInternal()
            override final;
    HalResult<std::vector<hardware::vibrator::CompositePrimitive>> getSupportedPrimitivesInternal()
            override final;
    HalResult<std::vector<std::chrono::milliseconds>> getPrimitiveDurationsInternal(
            const std::vector<hardware::vibrator::CompositePrimitive>& supportedPrimitives)
            override final;
    HalResult<std::chrono::milliseconds> getPrimitiveDelayMaxInternal() override final;
    HalResult<std::chrono::milliseconds> getPrimitiveDurationMaxInternal() override final;
    HalResult<int32_t> getCompositionSizeMaxInternal() override final;
    HalResult<int32_t> getPwleSizeMaxInternal() override final;
    HalResult<float> getMinFrequencyInternal() override final;
    HalResult<float> getResonantFrequencyInternal() override final;
    HalResult<float> getFrequencyResolutionInternal() override final;
    HalResult<float> getQFactorInternal() override final;
    HalResult<std::vector<float>> getMaxAmplitudesInternal() override final;

private:
    const std::function<HalResult<sp<hardware::vibrator::IVibrator>>()> mReconnectFn;
    std::mutex mHandleMutex;
    sp<hardware::vibrator::IVibrator> mHandle GUARDED_BY(mHandleMutex);

    sp<hardware::vibrator::IVibrator> getHal();
};

// Wrapper for the HDIL Vibrator HALs.
template <typename I>
class HidlHalWrapper : public HalWrapper {
public:
    HidlHalWrapper(std::shared_ptr<CallbackScheduler> scheduler, sp<I> handle)
          : HalWrapper(std::move(scheduler)), mHandle(std::move(handle)) {}
    virtual ~HidlHalWrapper() = default;

    HalResult<void> ping() override final;
    void tryReconnect() override final;

    HalResult<void> on(std::chrono::milliseconds timeout,
                       const std::function<void()>& completionCallback) override final;
    HalResult<void> off() override final;

    HalResult<void> setAmplitude(float amplitude) override final;
    virtual HalResult<void> setExternalControl(bool enabled) override;

    HalResult<void> alwaysOnEnable(int32_t id, hardware::vibrator::Effect effect,
                                   hardware::vibrator::EffectStrength strength) override final;
    HalResult<void> alwaysOnDisable(int32_t id) override final;

protected:
    std::mutex mHandleMutex;
    sp<I> mHandle GUARDED_BY(mHandleMutex);

    virtual HalResult<Capabilities> getCapabilitiesInternal() override;

    template <class T>
    using perform_fn =
            hardware::Return<void> (I::*)(T, hardware::vibrator::V1_0::EffectStrength,
                                          hardware::vibrator::V1_0::IVibrator::perform_cb);

    template <class T>
    HalResult<std::chrono::milliseconds> performInternal(
            perform_fn<T> performFn, sp<I> handle, T effect,
            hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback);

    sp<I> getHal();
};

// Wrapper for the HDIL Vibrator HAL v1.0.
class HidlHalWrapperV1_0 : public HidlHalWrapper<hardware::vibrator::V1_0::IVibrator> {
public:
    HidlHalWrapperV1_0(std::shared_ptr<CallbackScheduler> scheduler,
                       sp<hardware::vibrator::V1_0::IVibrator> handle)
          : HidlHalWrapper<hardware::vibrator::V1_0::IVibrator>(std::move(scheduler),
                                                                std::move(handle)) {}
    virtual ~HidlHalWrapperV1_0() = default;

    HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) override final;
};

// Wrapper for the HDIL Vibrator HAL v1.1.
class HidlHalWrapperV1_1 : public HidlHalWrapper<hardware::vibrator::V1_1::IVibrator> {
public:
    HidlHalWrapperV1_1(std::shared_ptr<CallbackScheduler> scheduler,
                       sp<hardware::vibrator::V1_1::IVibrator> handle)
          : HidlHalWrapper<hardware::vibrator::V1_1::IVibrator>(std::move(scheduler),
                                                                std::move(handle)) {}
    virtual ~HidlHalWrapperV1_1() = default;

    HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) override final;
};

// Wrapper for the HDIL Vibrator HAL v1.2.
class HidlHalWrapperV1_2 : public HidlHalWrapper<hardware::vibrator::V1_2::IVibrator> {
public:
    HidlHalWrapperV1_2(std::shared_ptr<CallbackScheduler> scheduler,
                       sp<hardware::vibrator::V1_2::IVibrator> handle)
          : HidlHalWrapper<hardware::vibrator::V1_2::IVibrator>(std::move(scheduler),
                                                                std::move(handle)) {}
    virtual ~HidlHalWrapperV1_2() = default;

    HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) override final;
};

// Wrapper for the HDIL Vibrator HAL v1.3.
class HidlHalWrapperV1_3 : public HidlHalWrapper<hardware::vibrator::V1_3::IVibrator> {
public:
    HidlHalWrapperV1_3(std::shared_ptr<CallbackScheduler> scheduler,
                       sp<hardware::vibrator::V1_3::IVibrator> handle)
          : HidlHalWrapper<hardware::vibrator::V1_3::IVibrator>(std::move(scheduler),
                                                                std::move(handle)) {}
    virtual ~HidlHalWrapperV1_3() = default;

    HalResult<void> setExternalControl(bool enabled) override final;

    HalResult<std::chrono::milliseconds> performEffect(
            hardware::vibrator::Effect effect, hardware::vibrator::EffectStrength strength,
            const std::function<void()>& completionCallback) override final;

protected:
    HalResult<Capabilities> getCapabilitiesInternal() override final;
};

// -------------------------------------------------------------------------------------------------

}; // namespace vibrator

}; // namespace android

#endif // ANDROID_OS_VIBRATORHALWRAPPER_H
