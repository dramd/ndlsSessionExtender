#pragma once
#include <JuceHeader.h>

namespace db
{
    class CouchbaseLiteDatabase;
}

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
    juce::TextButton bigRedButton { "Go!" };
    std::unique_ptr<db::CouchbaseLiteDatabase> db;
    std::unique_ptr<juce::FileChooser> fileChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
