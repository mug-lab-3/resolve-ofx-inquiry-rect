// Minimal OFX plugin built to inquire with Blackmagic about two DaVinci Resolve
// host behaviours. The plugin renders a centred rectangle whose size is a direct
// function of arg.time, and offers a button that sets a keyframe at time 0, so
// both issues below can be observed/reproduced visually.
//
// 1. The rectangle size is animated purely from RenderArguments::time, so in
//    normal use it changes continuously as the clip plays. However, when in/out
//    points are set and a clip is placed within that in/out range, the size no
//    longer changes during playback: the rectangle stays frozen. The cause is
//    that render() is always handed time=0 in this case, instead of the clip's
//    actual timeline frame. See the size computation in render() below, which
//    depends only on p_Args.time.
//
// 2. Cut a clip so it becomes two separate clips, then operate on the second
//    (trailing) clip. The two are distinct clips, so setting a keyframe at
//    frame 0 should land on that second clip's own frame 0 (expected). Instead,
//    the keyframe is placed at frame 0 of the first (leading) clip.
//    => Reproduced by pressing the "Key Size Scale at 0" button on the second
//       clip, which calls setValueAtTime(0.0, 1.0) in changedParam() below.

#include "InquiryPlugin.h"

#include <cmath>
#include <memory>

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

////////////////////////////////////////////////////////////////////////////////

class InquiryPlugin : public OFX::ImageEffect {
   public:
    explicit InquiryPlugin(OfxImageEffectHandle p_Handle) : ImageEffect(p_Handle) {
        m_DstClip = fetchClip(kOfxImageEffectOutputClipName);
        m_SizeScale = fetchDoubleParam(kParamSizeScale);
    }

    virtual void changedParam(const OFX::InstanceChangedArgs& /*p_Args*/, const std::string& p_ParamName) {
        if (p_ParamName == kParamResetSize) {
            // Set a keyframe of value 1.0 at time 0 on the size coefficient.
            m_SizeScale->setValueAtTime(0.0, 1.0);
        }
    }

    virtual void render(const OFX::RenderArguments& p_Args) {
        if ((m_DstClip->getPixelDepth() != OFX::eBitDepthFloat) ||
            (m_DstClip->getPixelComponents() != OFX::ePixelComponentRGBA)) {
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }

        std::unique_ptr<OFX::Image> dst(m_DstClip->fetchImage(p_Args.time));
        const OfxRectI bounds = dst->getBounds();
        const int cx = (bounds.x1 + bounds.x2) / 2;
        const int cy = (bounds.y1 + bounds.y2) / 2;

        // Rectangle size is a direct function of arg.time. A 100-frame triangle
        // wave ramps the box between 10% and 50% of the frame, phase-shifted a
        // quarter period so time=0 lands mid-size.
        constexpr double kPeriod = 100.0;
        const double phased = p_Args.time + (kPeriod * 0.25);
        const double tri = 1.0 - std::abs((std::fmod(phased, kPeriod) / (kPeriod * 0.5)) - 1.0);
        const double fraction = (0.1 + 0.4 * tri) * m_SizeScale->getValueAtTime(p_Args.time);
        const int halfWidth = static_cast<int>((bounds.x2 - bounds.x1) * fraction * 0.5);
        const int halfHeight = static_cast<int>((bounds.y2 - bounds.y1) * fraction * 0.5);

        const OfxRectI win = p_Args.renderWindow;
        for (int y = win.y1; y < win.y2; ++y) {
            const bool insideY = std::abs(y - cy) <= halfHeight;
            for (int x = win.x1; x < win.x2; ++x) {
                float* pix = static_cast<float*>(dst->getPixelAddress(x, y));
                if (!pix) continue;
                const float v = (insideY && std::abs(x - cx) <= halfWidth) ? 1.0f : 0.0f;
                pix[0] = pix[1] = pix[2] = pix[3] = v;
            }
        }
    }

   private:
    OFX::Clip* m_DstClip;
    OFX::DoubleParam* m_SizeScale;
};

////////////////////////////////////////////////////////////////////////////////

using namespace OFX;

InquiryPluginFactory::InquiryPluginFactory()
    : OFX::PluginFactoryHelper<InquiryPluginFactory>(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor) {
}

void InquiryPluginFactory::describe(OFX::ImageEffectDescriptor& p_Desc) {
    p_Desc.setLabels(kPluginName, kPluginName, kPluginName);
    p_Desc.setPluginGrouping(kPluginGrouping);
    p_Desc.setPluginDescription(kPluginDescription);

    // Both a generator and an effect.
    p_Desc.addSupportedContext(eContextGenerator);
    p_Desc.addSupportedContext(eContextFilter);
    p_Desc.addSupportedContext(eContextGeneral);

    p_Desc.addSupportedBitDepth(eBitDepthFloat);
}

void InquiryPluginFactory::describeInContext(OFX::ImageEffectDescriptor& p_Desc, OFX::ContextEnum /*p_Context*/) {
    // Source clip is always defined but optional, so the generator context (no
    // connected input) still triggers render and clip access stays uniform.
    ClipDescriptor* srcClip = p_Desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setOptional(true);

    p_Desc.defineClip(kOfxImageEffectOutputClipName)->addSupportedComponent(ePixelComponentRGBA);

    PageParamDescriptor* page = p_Desc.definePageParam("Controls");

    // Rectangle size coefficient.
    DoubleParamDescriptor* sizeScale = p_Desc.defineDoubleParam(kParamSizeScale);
    sizeScale->setLabels("Size Scale", "Size Scale", "Size Scale");
    sizeScale->setHint("Coefficient applied to the rectangle size.");
    sizeScale->setDefault(1.0);
    sizeScale->setRange(0.0, 10.0);
    sizeScale->setDisplayRange(0.0, 2.0);

    // Button that keyframes the size coefficient to 1.0 at time 0.
    PushButtonParamDescriptor* reset = p_Desc.definePushButtonParam(kParamResetSize);
    reset->setLabels("Key Size Scale at 0", "Key Size Scale at 0", "Key Size Scale at 0");
    reset->setHint("Set a keyframe of value 1.0 on the size coefficient at time 0.");
    page->addChild(*reset);
}

ImageEffect* InquiryPluginFactory::createInstance(OfxImageEffectHandle p_Handle, ContextEnum /*p_Context*/) {
    return new InquiryPlugin(p_Handle);
}

void OFX::Plugin::getPluginIDs(PluginFactoryArray& p_FactoryArray) {
    static InquiryPluginFactory inquiryPlugin;
    p_FactoryArray.push_back(&inquiryPlugin);
}
