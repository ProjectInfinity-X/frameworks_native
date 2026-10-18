/*
 * Copyright 2026 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/BufferItemConsumer.h>
#include <gui/Surface.h>
#include <gui/mock/GraphicBufferProducer.h>
#include <hardware/gralloc.h>

#include "DisplayHardware/VirtualDisplay/LegacyVirtualDisplaySurface.h"
#include "mock/DisplayHardware/MockHWComposer.h"

namespace android {

using testing::_;
using testing::Return;
using testing::StrictMock;

class LegacyVirtualDisplaySurfaceTest : public testing::Test {
protected:
    void createDisplay(uint64_t sinkUsage, bool secure = false) {
        auto [consumer, surface] = BufferItemConsumer::create(sinkUsage);
        mSinkConsumer = std::move(consumer);
        mDisplay = sp<LegacyVirtualDisplaySurface>::make(mHwc, HalVirtualDisplayId(123), surface,
                                                         "LegacyVirtualDisplayTest", secure);
    }

    uint64_t outputUsage() const { return mDisplay->mOutputUsage; }
    void setOutputUsage(uint64_t usage) { mDisplay->setOutputUsage(usage); }

    void prepareCommit(const sp<IGraphicBufferProducer>& sink, bool mustRecompose) {
        mDisplay->mSource[LegacyVirtualDisplaySurface::SOURCE_SINK] = sink;
        mDisplay->mCompositionType = compositionengine::DisplaySurface::CompositionType::Hwc;
        mDisplay->mDebugState = LegacyVirtualDisplaySurface::DebugState::Hwc;
        mDisplay->mOutputProducerSlot = 0;
        mDisplay->mMustRecompose = mustRecompose;
    }

    StrictMock<mock::HWComposer> mHwc;
    sp<BufferItemConsumer> mSinkConsumer;
    sp<LegacyVirtualDisplaySurface> mDisplay;
};

TEST_F(LegacyVirtualDisplaySurfaceTest, preserves64BitConsumerUsage) {
    constexpr uint64_t kVendorUsage = uint64_t{1} << 40;
    createDisplay(kVendorUsage);
    EXPECT_EQ(kVendorUsage | GRALLOC_USAGE_HW_COMPOSER, outputUsage());
}

TEST_F(LegacyVirtualDisplaySurfaceTest, combinesRendererAndConsumerUsage) {
    createDisplay(GRALLOC_USAGE_HW_VIDEO_ENCODER);
    setOutputUsage(GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER);
    EXPECT_EQ(GRALLOC_USAGE_HW_VIDEO_ENCODER | GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER,
              outputUsage());
}

TEST_F(LegacyVirtualDisplaySurfaceTest, secureEncoderUsageRemainsProtected) {
    createDisplay(GRALLOC_USAGE_HW_VIDEO_ENCODER, true);
    setOutputUsage(GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER);
    EXPECT_EQ(GRALLOC_USAGE_HW_VIDEO_ENCODER | GRALLOC_USAGE_HW_COMPOSER | GRALLOC_USAGE_HW_RENDER |
                      GRALLOC_USAGE_PROTECTED,
              outputUsage());
}

TEST_F(LegacyVirtualDisplaySurfaceTest, queuesRecomposedFrameWithoutPresentFence) {
    createDisplay(0);
    auto sink = sp<StrictMock<mock::GraphicBufferProducer>>::make();
    prepareCommit(sink, true);
    EXPECT_CALL(mHwc, getPresentFence(HalDisplayId(HalVirtualDisplayId(123))))
            .WillOnce(Return(Fence::NO_FENCE));
    EXPECT_CALL(*sink, queueBuffer(0, _, _)).WillOnce(Return(OK));
    EXPECT_CALL(*sink, cancelBuffer(_, _)).Times(0);

    mDisplay->onFrameCommitted();
}

TEST_F(LegacyVirtualDisplaySurfaceTest, cancelsUnchangedFrameWithoutPresentFence) {
    createDisplay(0);
    auto sink = sp<StrictMock<mock::GraphicBufferProducer>>::make();
    prepareCommit(sink, false);
    EXPECT_CALL(mHwc, getPresentFence(HalDisplayId(HalVirtualDisplayId(123))))
            .WillOnce(Return(Fence::NO_FENCE));
    EXPECT_CALL(*sink, queueBuffer(_, _, _)).Times(0);
    EXPECT_CALL(*sink, cancelBuffer(0, _)).WillOnce(Return(OK));

    mDisplay->onFrameCommitted();
}

} // namespace android
