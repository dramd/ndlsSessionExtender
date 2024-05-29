#include "MainComponent.h"
#include "CouchbaseLite.h"

static juce::File getEndlesssGlobalDatabase() {
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory).getChildFile("Endlesss").getChildFile("production").getChildFile("Data").getChildFile("global.cblite2");
};
/** Someone should have invented a new endlesss by now */
static juce::Time getY2038() { return { 2038, 1, 19,  3,  14,  7, 0, false }; }

static juce::StringArray defaultRoles()
{
    juce::StringArray out;
    out.add("user");

    return out;
}

static
juce::Result
setExpires
(
    juce::DynamicObject &doc_session,
    juce::Time           time
) {
    doc_session.setProperty("expires", getY2038().toMilliseconds());
    return juce::Result::ok();
}

static
juce::var
createSession
(
    juce::String      user_id,
    juce::StringArray jams        = {},
    juce::StringArray roles       = defaultRoles(),
    juce::Time        expiry_date = getY2038()
) {
    juce::Time created_and_issued = juce::Time::getCurrentTime() - juce::RelativeTime::minutes(1);

    // -------------------------------------------------------------------------------
    // Couch DB connection data that will be useless and ignored
    juce::String couchdb_token    = "some-couch-db-token";
    juce::String couchdb_password = "some-couch-db-session-pw";

    juce::DynamicObject *user_dbs_obj = new juce::DynamicObject();
    juce::String appdata_url = juce::String("https://") + couchdb_token + ":" + couchdb_password + "@data.endlesss.fm/user_appdata$" + user_id;
    user_dbs_obj->setProperty("appdata", appdata_url);

    // -------------------------------------------------------------------------------
    // Profile data that just needs to match (mostly)
    juce::DynamicObject *profile_obj = new juce::DynamicObject();
    profile_obj->setProperty("type",          "user");
    profile_obj->setProperty("bands",         jams);
    profile_obj->setProperty("bio",           "Your bio that nobody can see now :(");
    profile_obj->setProperty("displayName",   user_id);
    profile_obj->setProperty("fullName",      user_id);

    // -------------------------------------------------------------------------------
    // Here comes the session
    juce::DynamicObject *obj = new juce::DynamicObject();

    obj->setProperty("type",           "Session");
    obj->setProperty("app_version",    10000);

    obj->setProperty("user_id",        user_id);
    obj->setProperty("roles",          roles);
    obj->setProperty("profile",        juce::var(profile_obj));

    obj->setProperty("created",        created_and_issued.toMilliseconds());
    obj->setProperty("issued",         created_and_issued.toMilliseconds());
    obj->setProperty("expires",        expiry_date.toMilliseconds());

    obj->setProperty("hash",           "");
    obj->setProperty("ip",             "192.138.0.0");
    obj->setProperty("isGuest",        false);
    obj->setProperty("licenses",       juce::var());
    obj->setProperty("provider",       "local");
    obj->setProperty("token",          "some-couch-db-token");
    obj->setProperty("password",       "some-couch-db-session-pw");
    obj->setProperty("userDBs",        juce::var(user_dbs_obj));

    return juce::var(obj);
}



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
    
    bigRedButton.onClick = [this] 
    {
        if(db)
        {
            auto onMessageBoxResult = [this](int result)
            {
                if(result != 0)
                {
                    return;
                }

                if(!db)
                {
                    return;
                }

                juce::Result updateResult = updateActiveSession();
                if(updateResult.failed())
                {
                    DBG(updateResult.getErrorMessage());
                }
                else
                {
                    setupButtonText();
                }
            };
            
            juce::NativeMessageBox::showAsync
            (
                juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("Are you sure?")
                .withMessage ("This is an unofficial tool that modifies Endlesss data and may break things!")
                .withButton ("OK")
                .withButton ("Cancel")
                .withAssociatedComponent (this),
                onMessageBoxResult
            );
        }
        else
        {
            fileChooser = std::make_unique<juce::FileChooser> 
            (
                "Please select Endlesss global.cblite2",
                juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory),
                "*.cblite2",
                true,
                true
            );

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
        juce::var document = db->getLocalDocument("ActiveSession");
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
    if(!db)
    {
        return juce::Result::fail("Database file could not be opened");
    }

    juce::var document = db->getLocalDocument("ActiveSession");
    if(!document.isObject())
    {
        return juce::Result::fail("Did not find an active session");
    }

    if(juce::DynamicObject *object = document.getDynamicObject())
    {
        setExpires(*object, getY2038());
    }
    db->setLocalDocument(document);
    return juce::Result::ok();
}
//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
}

void MainComponent::resized()
{
    bigRedButton.setBounds(getLocalBounds());
}
