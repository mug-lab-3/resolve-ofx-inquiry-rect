#pragma once

#include "ofxsImageEffect.h"

class InquiryPluginFactory : public OFX::PluginFactoryHelper<InquiryPluginFactory>
{
public:
    InquiryPluginFactory();
    virtual void load() {}
    virtual void unload() {}
    virtual void describe(OFX::ImageEffectDescriptor& desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};
