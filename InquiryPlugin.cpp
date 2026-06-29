// A plugin built to confirm behaviours that appear to be bugs in DaVinci
// Resolve 21.
// It renders a centred rectangle whose size varies with arg.time, and also
// offers a button that sets a keyframe at time 0, so the two issues below can be
// confirmed/reproduced visually.
//
// DaVinci Resolve 21での不具合と思われる挙動を確認するためのプラグイン
// 中央に四角形を描画し、そのサイズは arg.time に連動して変化します
// また time 0 の位置にキーフレームを設定するボタンも備えており、
// 下記 2 つの問題を視覚的に確認・再現できます。
//
// 1. The rectangle size is determined purely from RenderArguments::time, so in
//    normal use it changes continuously as the clip plays. However, when in/out
//    points are set and a clip is placed within that in/out range, the size no
//    longer changes during playback: the rectangle stays frozen. The cause is
//    that render() is handed an invalid time (such as 0 or a negative value)
//    fixed to a single value, so the clip's actual time never arrives.
//
//    四角形のサイズは RenderArguments::time のみから決まるため、通常はクリップ
//    の再生に伴って連続的に変化します。しかし in/out 点を設定し、その in/out 区間
//    内にクリップを配置すると、再生してもサイズが変化せず四角形が固まったまま
//    になる。原因は、render() に渡される time が不正な値(0 や負の値など)で固定
//    され、クリップの実際の時間が渡ってこないためです。
//
// 2. Cut a clip so it becomes two separate clips, then operate on the second
//    (trailing) clip. On the second clip, use the "Set key frame" button to set
//    a keyframe at time 0. The expected behaviour is for the keyframe to
//    land on the second clip's own time 0. Instead, it is placed at time 0 of
//    the first (leading) clip. For a split clip, the time origin seems to remain
//    at the pre-split position.
//
//    クリップをカットして2つの別クリップに分割し、2番目(後ろ)のクリップを操作します。
//    2番目クリップで、`Set key frame`ボタンを使用してtime 0にキーフレームを設定します。
//    期待値動作は 2番目のクリップのtime 0 にキーフレームが設定されることです。
//    しかし実際には1番目(前)のクリップのtime 0 の位置にキーフレームが設定されてしまう。
//    分割されたクリップにおいて起点が分割前の位置になってしまっているようです。

#include "InquiryPlugin.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>

#include "ofxsImageEffect.h"

#define kPluginName "Inquiry Rect"
#define kPluginGrouping "Resolve Inquiry"
#define kPluginDescription                                                      \
    "Draws a solid rectangle whose size depends on arg.time. Built to inquire " \
    "about Resolve host behaviour (Generator vs Effect context)."
#define kPluginIdentifier "com.resolve.inquiry.rect"
#define kPluginVersionMajor 1
#define kPluginVersionMinor 0

#define kParamSizeScale "sizeScale"
#define kParamResetSize "resetSize"
#define kParamShowState "showState"
#define kParamStateText "stateText"

#define kStateHistory 5

////////////////////////////////////////////////////////////////////////////////

namespace {

struct CallRecord {
    std::chrono::system_clock::time_point wallClock_;
    double argsTime_;
};

auto formatState(uint64_t pCallCount, const std::array<CallRecord, kStateHistory>& pHistory, int pHead) -> std::string {
    std::ostringstream out;

    const uint64_t shown = std::min<uint64_t>(pCallCount, kStateHistory);
    for (uint64_t i = 0; i < shown; ++i) {
        // Walk from the oldest retained entry to the newest.
        const int idx = (pHead + kStateHistory - static_cast<int>(shown) + static_cast<int>(i)) % kStateHistory;
        const CallRecord& rec = pHistory[idx];

        const std::time_t secs = std::chrono::system_clock::to_time_t(rec.wallClock_);
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(rec.wallClock_.time_since_epoch()).count() % 1000;
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &secs);
#else
        localtime_r(&secs, &tm);
#endif
        std::array<char, 16> clockStr{};
        std::snprintf(clockStr.data(), clockStr.size(), "%02d:%02d:%02d.%03lld", tm.tm_hour, tm.tm_min, tm.tm_sec,
                      static_cast<long long>(millis));
        out << clockStr.data() << "  " << rec.argsTime_ << "\n";
    }
    return out.str();
}

}  // namespace

class InquiryPlugin : public OFX::ImageEffect {
   public:
    explicit InquiryPlugin(OfxImageEffectHandle pHandle) : ImageEffect(pHandle) {
        mDstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        mSizeScale_ = fetchDoubleParam(kParamSizeScale);
        mStateText_ = fetchStringParam(kParamStateText);
    }

    void changedParam(const OFX::InstanceChangedArgs& /*args*/, const std::string& paramName) override {
        if (paramName == kParamResetSize) {
            // Set a keyframe of value 1.0 at time 0 on the size coefficient.
            mSizeScale_->setValueAtTime(0.0, 1.0);
        } else if (paramName == kParamShowState) {
            mStateText_->setValue(formatState(mCallCount_, mHistory_, mHistoryHead_));
        }
    }

    void getClipPreferences(OFX::ClipPreferencesSetter& clipPreferences) override {
        clipPreferences.setOutputFrameVarying(true);
    }

    void render(const OFX::RenderArguments& args) override {
        recordCall(args.time);

        if ((mDstClip_->getPixelDepth() != OFX::eBitDepthFloat) ||
            (mDstClip_->getPixelComponents() != OFX::ePixelComponentRGBA)) {
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }

        std::unique_ptr<OFX::Image> dst(mDstClip_->fetchImage(args.time));
        const OfxRectI bounds = dst->getBounds();
        const int cx = (bounds.x1 + bounds.x2) / 2;
        const int cy = (bounds.y1 + bounds.y2) / 2;

        // Rectangle size is a direct function of arg.time. A 100-frame triangle
        // wave ramps the box between 10% and 50% of the frame, phase-shifted a
        // quarter period so time=0 lands mid-size.
        constexpr double kPeriod = 100.0;
        const double phased = args.time + (kPeriod * 0.25);
        const double tri = 1.0 - std::abs((std::fmod(phased, kPeriod) / (kPeriod * 0.5)) - 1.0);
        const double fraction = (0.1 + (0.4 * tri)) * mSizeScale_->getValueAtTime(args.time);
        const int halfWidth = static_cast<int>((bounds.x2 - bounds.x1) * fraction * 0.5);
        const int halfHeight = static_cast<int>((bounds.y2 - bounds.y1) * fraction * 0.5);

        const OfxRectI win = args.renderWindow;
        for (int y = win.y1; y < win.y2; ++y) {
            const bool insideY = std::abs(y - cy) <= halfHeight;
            for (int x = win.x1; x < win.x2; ++x) {
                auto* pix = static_cast<float*>(dst->getPixelAddress(x, y));
                if (pix == nullptr) {
                    continue;
                }
                const float v = (insideY && std::abs(x - cx) <= halfWidth) ? 1.0F : 0.0F;
                pix[0] = pix[1] = pix[2] = pix[3] = v;
            }
        }
    }

   private:
    // Append one render() call to the ring buffer of the most recent calls.
    void recordCall(double argsTime) {
        mHistory_[mHistoryHead_] = {.wallClock_ = std::chrono::system_clock::now(), .argsTime_ = argsTime};
        mHistoryHead_ = (mHistoryHead_ + 1) % kStateHistory;
        ++mCallCount_;
    }

    OFX::Clip* mDstClip_;
    OFX::DoubleParam* mSizeScale_;
    OFX::StringParam* mStateText_;

    std::array<CallRecord, kStateHistory> mHistory_{};
    int mHistoryHead_ = 0;
    uint64_t mCallCount_ = 0;
};

////////////////////////////////////////////////////////////////////////////////

using OFX::PluginFactoryArray;

InquiryPluginFactory::InquiryPluginFactory()
    : OFX::PluginFactoryHelper<InquiryPluginFactory>(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {
}

void InquiryPluginFactory::describe(OFX::ImageEffectDescriptor& desc) {
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Both a generator and an effect.
    desc.addSupportedContext(OFX::eContextGenerator);
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
}

void InquiryPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum /*context*/) {
    OFX::ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setOptional(true);

    desc.defineClip(kOfxImageEffectOutputClipName)->addSupportedComponent(OFX::ePixelComponentRGBA);

    OFX::PageParamDescriptor* page = desc.definePageParam("Controls");

    OFX::DoubleParamDescriptor* sizeScale = desc.defineDoubleParam(kParamSizeScale);
    sizeScale->setLabels("Size Scale", "Size Scale", "Size Scale");
    sizeScale->setHint("Coefficient applied to the rectangle size.");
    sizeScale->setDefault(1.0);
    sizeScale->setRange(0.0, 10.0);
    sizeScale->setDisplayRange(0.0, 2.0);

    OFX::PushButtonParamDescriptor* reset = desc.definePushButtonParam(kParamResetSize);
    reset->setLabels("Set key frame", "Set key frame", "Set key frame");
    reset->setHint("Set a keyframe of value 1.0 on the size coefficient at time 0.");
    page->addChild(*reset);

    OFX::PushButtonParamDescriptor* showState = desc.definePushButtonParam(kParamShowState);
    showState->setLabels("Show internal state", "Show internal state", "Show internal state");
    showState->setHint("Write the render() call count and the most recent calls into the text field.");
    page->addChild(*showState);

    OFX::StringParamDescriptor* stateText = desc.defineStringParam(kParamStateText);
    stateText->setLabels("Internal State", "Internal State", "Internal State");
    stateText->setHint("Recent render() calls: the render time and the args time the host passed in.");
    stateText->setStringType(OFX::eStringTypeMultiLine);
    stateText->setEnabled(false);
    stateText->setDefault("(press \"Show internal state\")");
    page->addChild(*stateText);
}

auto InquiryPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) -> OFX::ImageEffect* {
    return new InquiryPlugin(handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& id) {
    static InquiryPluginFactory inquiryPlugin;
    id.push_back(&inquiryPlugin);
}
