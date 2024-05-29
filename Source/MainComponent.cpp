#include "MainComponent.h"
#include "CouchbaseLite.h"

static juce::File getEndlesssGlobalDatabase() 
{
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory).getChildFile("Endlesss").getChildFile("production").getChildFile("Data").getChildFile("global.cblite2");
};

static juce::File getGlobalDatabasePrototype()
{
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType:: currentApplicationFile).getChildFile("db.sqlite3");
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

static
juce::Result
createActiveSession
(
    db::CouchbaseLiteDatabase &db,
    juce::String               user_id
) {
    juce::var session_data = createSession(user_id);

    juce::DynamicObject *doc_obj = session_data.getDynamicObject();
    doc_obj->setProperty("_id", "ActiveSession");
    doc_obj->setProperty("_rev", "10000-local");

    int const num_rows_modified = db.setLocalDocument(session_data);

    if (num_rows_modified > 0)
    {
        return juce::Result::ok();
    }

    return juce::Result::fail("Session document could not be updated");
}

static
void
copyPrototypeDbWithBackup
(
    juce::File source,
    juce::File destination
) { 
    juce::File destination_backup = destination.withFileExtension(".sqlite3.backup");
    destination.copyFileTo(destination_backup);

    source.copyFileTo(destination);
}


//==============================================================================
MainComponent::MainComponent()
{
    setSize (320, 240);
    auto& lnf = getLookAndFeel();

    if(getEndlesssGlobalDatabase().exists())
    {
        db = std::make_unique<db::CouchbaseLiteDatabase>(getEndlesssGlobalDatabase());
        syncUiState();
    }
    
    btn_apply.onClick = [this] 
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

                copyPrototypeDbWithBackup(getGlobalDatabasePrototype(), getEndlesssGlobalDatabase().getChildFile("db.sqlite3"));

                juce::Result updateResult = createActiveSession(*db, editor_username.getText());

                
                juce::MessageBoxOptions opts;

                if(updateResult.failed())
                {
                    opts = opts.withIconType(juce::MessageBoxIconType::WarningIcon)
                    .withTitle("Error")
                    .withMessage(updateResult.getErrorMessage())
                    .withButton("OK")
                    .withAssociatedComponent(this);
                }
                else
                {
                    opts = opts.withIconType(juce::MessageBoxIconType::InfoIcon)
                    .withTitle("Success")
                    .withMessage("Session document updated")
                    .withButton("OK")
                    .withAssociatedComponent(this);
                }

                juce::NativeMessageBox::showAsync(opts, nullptr);

                syncUiState();
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
                    syncUiState();
                }
            });
        }
    };

    addAndMakeVisible(btn_apply);
    addAndMakeVisible(editor_username);

    auto editor_username_callback = [this]()
    {
        syncUiState();
    };

    editor_username.onTextChange = editor_username_callback;
    editor_username.onReturnKey  = editor_username_callback;
    editor_username.onEscapeKey  = editor_username_callback;
    editor_username.onFocusLost  = editor_username_callback;
    
    editor_username.setJustification(juce::Justification::centred);

    
    btn_apply.setColour(juce::TextButton::ColourIds::buttonColourId,   juce::Colours::darkgrey);
    btn_apply.setColour(juce::TextButton::ColourIds::buttonOnColourId, juce::Colours::dimgrey);
    btn_apply.setColour(juce::TextButton::ColourIds::textColourOffId,  juce::Colours::white);
    btn_apply.setColour(juce::TextButton::ColourIds::textColourOnId,   juce::Colours::white);


    syncUiState();
}

MainComponent::~MainComponent()
{
}

void MainComponent::syncUiState()
{
    if(db)
    {
        editor_username.setEnabled(true);
        editor_username.setTextToShowWhenEmpty("Username", juce::Colours::grey);
        juce::var document = db->getLocalDocument("ActiveSession");

        juce::String username = editor_username.getText();
        btn_apply.setEnabled(username.isNotEmpty());

        btn_apply.setButtonText
        (
            username.isNotEmpty() ? "Create session for " + editor_username.getText()
                                  : "Enter your username in the textbox above"
        );
    }
    else
    {
        editor_username.setEnabled(false);
        editor_username.setTextToShowWhenEmpty("Use the button below to browse for your local database file", juce::Colours::grey);
        btn_apply.setButtonText("Browse for global.cblite2");
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
    juce::Rectangle<int> r = getLocalBounds();

    editor_username.setBounds(r.removeFromTop(32));
    r.removeFromTop(32);
    r.reduce(16, 16);
    btn_apply.setBounds(r);
}
