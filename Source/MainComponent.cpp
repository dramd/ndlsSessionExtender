#include "MainComponent.h"
#include "CouchbaseLite.h"
#include "globaldb.h"


static juce::File getGlobalDatabaseContainingFolder()
{
    auto file = juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory);
#ifdef JUCE_MAC
    if(file.getChildFile("Application Support").exists())
    {
        file = file.getChildFile("Application Support");
    }
#endif
    if(file.getChildFile("Endlesss").exists())
    {
        file = file.getChildFile("Endlesss");
    }
    if(file.getChildFile("production").exists())
    {
        file = file.getChildFile("production");
    }
    if(file.getChildFile("Data").exists())
    {
        file = file.getChildFile("Data");
    }

    return file;
}

static juce::File getEndlesssGlobalDatabase()
{
    return getGlobalDatabaseContainingFolder().getChildFile("global.cblite2");
};


static juce::File getGlobalDatabasePrototype()
{
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType:: currentApplicationFile).getSiblingFile("db.sqlite3");
#else
    return juce::File::getSpecialLocation(juce::File::SpecialLocationType:: currentApplicationFile).getChildFile("db.sqlite3");
#endif
};

static juce::File writeGlobalDbFile ()
{
    DBG("writeGlobalDbFile");
    if(getGlobalDatabasePrototype().existsAsFile()){
        DBG("Global Database Prototype already exists @ " << getGlobalDatabasePrototype().getFullPathName());
        return getGlobalDatabasePrototype();
    }
    auto destinationFile = getGlobalDatabasePrototype();
    DBG("Creating Global Database File from binary data @ " << destinationFile.getFullPathName());
    {
        auto outputStream = destinationFile.createOutputStream();
        outputStream->write(db_sqlite3, db_sqlite3Size);
        outputStream->flush();
    }
    DBG("Created Global Database File from binary data @ " << destinationFile.getFullPathName());
    return destinationFile;
}

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
    DBG("Setting expiry date");
    doc_session.setProperty("expires", getY2038().toMilliseconds());
    return juce::Result::ok();
}

static
juce::Result
setUser
(
    juce::DynamicObject &doc_session,
    juce::String           userId
) {
    if(userId.isNotEmpty() && userId.containsNonWhitespaceChars()){
        userId = userId.toLowerCase().trim();
        if(doc_session.getProperty("user_id") != userId){
            DBG("Setting user id: " << userId);
            doc_session.setProperty("user_id", userId);
        }
        return juce::Result::ok();
    }
    return juce::Result::fail("Invalid User ID");
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

    setUser(*obj, user_id);

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
    juce::String               user_id,
    juce::StringArray          roles
) {
    DBG("Creating Active Session for " << user_id);
    juce::var session_data = createSession(user_id, {}, roles);

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

    if (!destination_backup.existsAsFile())
    {
        DBG("Doing first run copy of original database file");
        // First time use! This means the current version of the global DB is precious.
        // Let's have it as an extra backup on the side.
        juce::File vip_backup = destination.withFileExtension(".sqlite3.original");
        destination.copyFileTo(vip_backup);
    }
    DBG("Making backup of database file");
    destination.copyFileTo(destination_backup);
    if(source != destination){
        DBG("Copying " << source.getFullPathName() << " to " << destination.getFullPathName());
        source.copyFileTo(destination);
    }
}
juce::Result getActiveUser(db::CouchbaseLiteDatabase& db, juce::String& username)
{
    try
    {
        DBG("Attempting to get currently active user id");
        juce::var document = db.getLocalDocument("ActiveSession");
        if(!document.isObject())
        {
            return juce::Result::fail("Did not find an active session");
        }
        if(document.hasProperty("user_id"))
        {
            username = document["user_id"];
        }
        else if(document.hasProperty("user"))
        {
            username = document["user"];
        }
        
        return juce::Result::ok();
    }
    catch (std::exception& e)
    {
        return juce::Result::fail("Exception occurred: " + juce::String(e.what()));
    }
    catch (...)
    {
        return juce::Result::fail("Unknown exception occured");
    }
}
juce::Result getRoles(db::CouchbaseLiteDatabase& db, juce::StringArray& roles)
{
    try
    {
        DBG("Attempting to get currently active user id");
        juce::var document = db.getLocalDocument("ActiveSession");
        if(!document.isObject())
        {
            roles = defaultRoles();
            return juce::Result::fail("Did not find an active session");
        }
        if(document.hasProperty("roles"))
        {
            if(document["roles"].isString())
                roles.add(document["roles"]);
            else if(document["roles"].isArray())
            {
                auto rolesVar = *document["roles"].getArray();
                for(auto role : rolesVar)
                {
                    if(role.isString())
                        roles.addIfNotAlreadyThere(role);
                }
            }
        }
        else
        {
            roles = defaultRoles();
        }
        return juce::Result::ok();
    }
    catch (std::exception& e)
    {
        return juce::Result::fail("Exception occurred: " + juce::String(e.what()));
    }
    catch (...)
    {
        return juce::Result::fail("Unknown exception occured");
    }
}

juce::Result updateActiveSession(db::CouchbaseLiteDatabase& db, const juce::String& username)
{
    try {
        DBG("Updating active session for " << username);

        juce::var document = db.getLocalDocument("ActiveSession");
        if(!document.isObject())
        {
            return juce::Result::fail("Did not find an active session");
        }
        
        if(juce::DynamicObject *object = document.getDynamicObject())
        {
            setUser(*object, username);
            setExpires(*object, getY2038());
        }
        int num_rows_modified = db.setLocalDocument(document);
        
        if (num_rows_modified > 0)
        {
            return juce::Result::ok();
        }

        return juce::Result::fail("Session document could not be updated");
    }
    catch (std::exception& e)
    {
        return juce::Result::fail("Exception occurred: " + juce::String(e.what()));
    }
    catch (...)
    {
        return juce::Result::fail("Unknown exception occured");
    }
}

//==============================================================================
MainComponent::MainComponent()
{
    setSize (320, 240);
    auto& lnf = getLookAndFeel();
    DBG("Initialising");
    roles = defaultRoles();
    if(getEndlesssGlobalDatabase().exists())
    {
        try 
        {
            DBG("Found global database on startup");
            db = std::make_unique<db::CouchbaseLiteDatabase>(getEndlesssGlobalDatabase());
            juce::String username;
            getActiveUser(*db, username);
            getRoles(*db, roles);
            if(username.isNotEmpty())
            {
                DBG("Found existing session with user id "<< username);
                editor_username.setText(username, juce::dontSendNotification);
            }
            writeGlobalDbFile ();
        }
        catch(std::exception e)
        {
            DBG("Startup Exception: " + juce::String(e.what()));
        }
        catch(...)
        {
            DBG("Startup Exception: Unknown");
        }
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

                juce::Result updateResult { juce::Result::fail("Failed to open database") };
                
                if(db)
                {
                    try
                    {
#if UPDATE_WITHOUT_REPLACING
                        copyPrototypeDbWithBackup(getEndlesssGlobalDatabase().getChildFile("db.sqlite3"), getEndlesssGlobalDatabase().getChildFile("db.sqlite3"));
                        updateResult = updateActiveSession(*db, editor_username.getText());
#else
                        copyPrototypeDbWithBackup(getGlobalDatabasePrototype(), getEndlesssGlobalDatabase().getChildFile("db.sqlite3"));
                        
                        updateResult = createActiveSession(*db, editor_username.getText(), roles);
#endif
                    } 
                    catch(std::exception e)
                    {
                        updateResult = juce::Result::fail(e.what());
                    } 
                    catch(...)
                    {
                        updateResult = juce::Result::fail("Unknown Exception");
                    }
                }
                juce::MessageBoxOptions opts;

                if(updateResult.failed())
                {
                    DBG("Error: " << updateResult.getErrorMessage());
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
                    .withMessage("Session document updated. You can now close this application and launch Endlesss.")
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
                .withMessage ("This is an unofficial tool that modifies Endlesss data and may break things! Make sure to close Endlesss before continuing.")
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
                getGlobalDatabaseContainingFolder(),
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
                    try {
                        DBG("Found global database provided by user");
                        db = std::make_unique<db::CouchbaseLiteDatabase>(databaseFile);
                        juce::String username;
                        getActiveUser(*db, username);
                        getRoles(*db, roles);
                        if(username.isNotEmpty())
                        {
                            DBG("Found existing session with user id "<< username);
                            editor_username.setText(username, juce::dontSendNotification);
                        }
                        syncUiState();
                    }
                    catch(std::exception e)
                    {
                        DBG(e.what());
                    } 
                    catch(...)
                    {
                        DBG("Unknown Exception");
                    }

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
    DBG("syncUiState");
    if(db)
    {
        DBG("Showing Create Session button");
        editor_username.setEnabled(true);
        editor_username.setTextToShowWhenEmpty("Username", juce::Colours::grey);

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
        DBG("Showing browse for global.cblite2");
        editor_username.setEnabled(false);
        editor_username.setTextToShowWhenEmpty("Use the button below to browse for your local database file", juce::Colours::grey);
        btn_apply.setButtonText("Browse for global.cblite2");
    }
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
