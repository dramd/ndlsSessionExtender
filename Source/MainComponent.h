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
    
    void syncUiState();


private:
    juce::TextEditor editor_username; 
    juce::TextButton btn_apply { "Go!" };
    juce::StringArray roles;
    std::unique_ptr<db::CouchbaseLiteDatabase> db;
    std::unique_ptr<juce::FileChooser> fileChooser;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
