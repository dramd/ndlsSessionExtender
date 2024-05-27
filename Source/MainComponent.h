#pragma once

#include "CouchbaseLite.h"
//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::Component
{
public:
    //==============================================================================
    MainComponent();
    ~MainComponent() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    void setupButtonText();

    juce::Result updateActiveSession();

private:
    //==============================================================================
    // Your private member variables go here...
    juce::TextButton bigRedButton { "Go!" };
    std::unique_ptr<db::CouchbaseLiteDatabase> db;
    std::unique_ptr<juce::FileChooser> fileChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
