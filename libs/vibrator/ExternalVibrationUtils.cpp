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
#include <cstring>

#include <math.h>

#include <vibrator/ExternalVibrationUtils.h>

namespace android::os {

namespace {
static constexpr float HAPTIC_SCALE_VERY_LOW_RATIO = 2.0f / 3.0f;
static constexpr float HAPTIC_SCALE_LOW_RATIO = 3.0f / 4.0f;
static constexpr float HAPTIC_MAX_AMPLITUDE_FLOAT = 1.0f;

float getHapticScaleGamma(HapticLevel level) {
    switch (level) {
    case HapticLevel::VERY_LOW:
        return 2.0f;
    case HapticLevel::LOW:
        return 1.5f;
    case HapticLevel::HIGH:
        return 0.5f;
    case HapticLevel::VERY_HIGH:
        return 0.25f;
    default:
        return 1.0f;
    }
}

float getHapticMaxAmplitudeRatio(HapticLevel level) {
    switch (level) {
    case HapticLevel::VERY_LOW:
        return HAPTIC_SCALE_VERY_LOW_RATIO;
    case HapticLevel::LOW:
        return HAPTIC_SCALE_LOW_RATIO;
    case HapticLevel::NONE:
    case HapticLevel::HIGH:
    case HapticLevel::VERY_HIGH:
        return 1.0f;
    default:
        return 0.0f;
    }
}

void applyHapticScale(float* buffer, size_t length, HapticScale scale) {
    if (scale.isScaleMute()) {
        memset(buffer, 0, length * sizeof(float));
        return;
    }
    if (scale.isScaleNone()) {
        return;
    }
    HapticLevel hapticLevel = scale.getLevel();
    float adaptiveScaleFactor = scale.getAdaptiveScaleFactor();
    float gamma = getHapticScaleGamma(hapticLevel);
    float maxAmplitudeRatio = getHapticMaxAmplitudeRatio(hapticLevel);

    for (size_t i = 0; i < length; i++) {
        if (hapticLevel != HapticLevel::NONE) {
            float sign = buffer[i] >= 0 ? 1.0 : -1.0;
            buffer[i] = powf(fabsf(buffer[i] / HAPTIC_MAX_AMPLITUDE_FLOAT), gamma)
                        * maxAmplitudeRatio * HAPTIC_MAX_AMPLITUDE_FLOAT * sign;
        }

        if (adaptiveScaleFactor != 1.0f) {
            buffer[i] *= adaptiveScaleFactor;
        }
    }
}

void clipHapticData(float* buffer, size_t length, float limit) {
    if (isnan(limit) || limit == 0) {
        return;
    }
    limit = fabsf(limit);
    for (size_t i = 0; i < length; i++) {
        float sign = buffer[i] >= 0 ? 1.0 : -1.0;
        if (fabsf(buffer[i]) > limit) {
            buffer[i] = limit * sign;
        }
    }
}

} // namespace

bool isValidHapticScale(HapticScale scale) {
    switch (scale.getLevel()) {
    case HapticLevel::MUTE:
    case HapticLevel::VERY_LOW:
    case HapticLevel::LOW:
    case HapticLevel::NONE:
    case HapticLevel::HIGH:
    case HapticLevel::VERY_HIGH:
        return true;
    }
    return false;
}

void scaleHapticData(float* buffer, size_t length, HapticScale scale, float limit) {
    if (isValidHapticScale(scale)) {
        applyHapticScale(buffer, length, scale);
    }
    clipHapticData(buffer, length, limit);
}

} // namespace android::os
