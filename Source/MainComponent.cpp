#include "MainComponent.h"

juce::File getEndlesssGlobalDatabase() {
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory).getChildFile("Endlesss").getChildFile("production").getChildFile("Data").getChildFile("global.cblite2");
};
/** Someone should have invented a new endlesss by now */
juce::Time getY2038() { return { 2038, 1, 19,  3,  14,  7, 0, false }; }
//==============================================================================
MainComponent::MainComponent()
{
    setSize (320, 240);
    auto& lnf = getLookAndFeel();
    lnf.setColour(juce::TextButton::ColourIds::buttonColourId, juce::Colours::red);

    if(getEndlesssGlobalDatabase().exists())
    {
        db = std::make_unique<db::CouchbaseLiteDatabase>(getEndlesssGlobalDatabase());
        setupButtonText();
    }
    
    bigRedButton.onClick = [this] {
        if(db)
        {
            auto onMessageBoxResult = [this](int result){
                if(result == 0)
                {
                    if(db)
                    {
                        auto result = updateActiveSession();
                        if(result.failed())
                        {
                            DBG(result.getErrorMessage());
                        }
                        else
                        {
                            setupButtonText();
                        }
                    }
                }
            };
            
            juce::NativeMessageBox::showAsync(juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Are you sure?")
                                              .withMessage ("This is an unofficial tool that modifies Endlesss data and may break things!")
                                              .withButton ("OK")
                                              .withButton ("Cancel")
                                              .withAssociatedComponent (this),
                                              onMessageBoxResult);
        }
        else
        {
            fileChooser = std::make_unique<juce::FileChooser> ("Please select Endlesss global.cblite2",
                                                               juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory),
                                                       "*.cblite2", true, true);

            auto folderChooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

            fileChooser->launchAsync (folderChooserFlags, [this] (const juce::FileChooser& chooser)
            {
                juce::File databaseFile (chooser.getResult());
                if(databaseFile.exists())
                {
                    db = std::make_unique<db::CouchbaseLiteDatabase>(databaseFile);
                    setupButtonText();
                }
            });
        }
    };
    setupButtonText();
    addAndMakeVisible(bigRedButton);
}

MainComponent::~MainComponent()
{
}
void MainComponent::setupButtonText()
{
    if(db)
    {
        auto document = db->getLocalDocument("ActiveSession");
        if(document.isObject() && document.hasProperty("user_id"))
        {
            juce::Time expiry { juce::int64(document["expires"]) };
            bigRedButton.setButtonText(document["user_id"].toString() + ": " + expiry.toISO8601(true));
        }
    }
    else
    {
        bigRedButton.setButtonText("Browse for global.cblite2");
    }
}

juce::Result MainComponent::updateActiveSession()
{
    if(db)
    {
        auto document = db->getLocalDocument("ActiveSession");
        if(!document.isObject())
            return juce::Result::fail("Did not find an active session");

        if(auto object = document.getDynamicObject())
        {
            object->setProperty("expires", getY2038().toMilliseconds());
        }
        db->setLocalDocument(document);
        return juce::Result::ok();
    }
    return juce::Result::fail("Database file could not be opened");
}
//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
}

void MainComponent::resized()
{
    // This is called when the MainComponent is resized.
    // If you add any child components, this is where you should
    // update their positions.
    bigRedButton.setBounds(getLocalBounds());
}
