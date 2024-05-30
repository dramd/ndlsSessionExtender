# NDLS Session Extender

*This tool and all code within is not affiliated with or endorsed by Endlesss Ltd. Use at your own risk.*

## The problem
Once the servers are down any login attempt from the Endlesss app will fail. Sadly, the application requires a valid session for your account to get past the login screen, whether you're connected to the internet or not. This makes even the solo jam inaccessible.

## The fix

We figured out a way to create or extend an account's local session (as far as the app is concerned) for an indefinite period of time. This doesn't make it usable online once the servers are shut off, but make it possible to access your solo jam (and some fully synced jams).

We recommend to log into your account from the latest version of Endlesss Studio before Friday (which by default gives you a session that is valid for 6 months) though this fix should make that unnecessary entirely. At least one step in this process will be *dramatically easier* if you are still logged in.

Also make sure to sync all the Soundpacks you want to keep. Importing them later is technically possible but a huge hassle you want to avoid.

In short:
- `This fix doesn't magically make any of the online features of Endlesss work. No servers = no service`
- This is for the `desktop client only`! On iOS this cannot be done manually or using the tool we'll provide.
- You should be able to still `use at least your solo jam` and possibly `some other jams`
- Ideally you are already logged into some account before applying this fix, but it's not required
- If you have multiple accounts, you will be able to switch between them later
- Please sync any soundpacks before server shutdown

Below is an explanation of what this tool does, as it can entirely be replicated manually if you have the right software and a little bit of knowledge about Sqlite and JSON.

In its current state, you can jam offline but Rifffs appear in your history with a delay. We have a potential solution for getting rid of this delay in the very near future. A proof of concept has already shown that it's possible.

## How to use this tool

**[Might add screenshots here to make this easier to follow]**

Close Endlesss if it is running.
Open the `ndlsSessionExtender` application.

It will do its best job to find where the database storing your session is on your device. This is why it's best to be logged in before the servers shut down, so that everything is in its usual state.

### Database could be located
If you see a textbox saying "Username" and a biggish button "Enter your username in the textbox above" you're good to go. Do so :)
Once you click the button "Create session for `entered username`" the app will do its thing (see next section). Then close the tool and open Endlesss.

### Database could not be located
If instead the big button says "Browse for global.cblite2" the tool either couldn't find it or you have never run Endlesss on this device. In the former case, no problem, click the button and it will take you to a file browser. Go to this location (some of these folders might be hidden)

- Windows: `C:\Users\<your windows username>\AppData\Roaming\Endlesss\production\Data\global.cblite2`
- macOS: `/Users/<your username>/Library/Application Support/Endlesss/production/Data/global.cblite2`

Note that you're supposed to select `global.cblite2`, not what's inside of it even though it's (technically) a folder. On Windows it's a folder, on macOS it might not look like it but it is a folder.

### I don't have that folder!!

If this path does not exist, you might be on a completely fresh installation on Endlesss. This is not impossible to solve, but very difficult at the current stage of this fix. Suggestion for now: If you're moving from one computer to another, copy anything below the `Endlesss` directory in that path from your old device to your new device. Alternatively, create that path manually and copy the file `db.sqlite3` into the `global.cblite2` folder. It may just get you through the door.  Then try again with the steps above.

## What the tool actually does

1. Makes a backup of your original, previous version of the database that is modified
2. Copies the database supplied along with the tool
3. Creates a brand new session document (based on the username entered) and inserts it into the database

The database file that comes with this tool matches exactly the last state of the official servers (you can verify it using any SQLite Browser). The reason this extra database is supplied is twofold:
- If you hadn't previously installed Endlesss, but you have an account, this might be enough to get you into the app
- to enable a future fix that will speed up the performance in your solo jam (there'll be an more info and an updated version)

## How the basic fix works

You do not have to use the tool in this repository in order to apply the fix. It can be done easily in just a few steps. The tool simply automates most of the process (exact differences described in the previous section). These are the steps to apply the fix manually. Read these steps once or twice all the way through before performing them! It is also advisable to make a backup copy of the `global.cblite2` file you will be editing.

### Extend an existing session

1. Download the [DB Browser for SQLite](https://sqlitebrowser.org/dl/) and install it
2. Open the following file using the DB Browser for SQlite
    - Windows: `C:\Users\<your windows username>\AppData\Roaming\Endlesss\production\Data\global.cblite2`
    - macOS: (please insert here if you know)
3. Navigate to the tab `Browse Data`
4. Select the table `localdocs`
5. You should see an entry with the the docid: `_local/ActiveSession` if you were previously logged in with your Endlesss application
    - If you do not see this entry, make sure to launch the app while the servers are still online and log in with your account.  Then repeat from step 2!
6. For that entry, click into the `json` column to reveal its content in the sidepanel titled `Edit Database Cell`
    - This should contain a bunch of data in text form, including things like `app_version`
7. In this bit of text, locate the value labeled `expires`. See the example below
8. The number next to it is a [https://www.epochconverter.com/](unix timestamp in milliseconds) describing when the app will assume your account's session has expired. This is when opening the app would no longer work. Replace this number with a unix timestamp that is waaaaay into the future, like `2150162047000`
    - This bears repeating: The timestamp is based on milliseconds! If you take a timestamp based on seconds it will likely be too small and thus cause your next launch of the app to expire the session immediately. If you're not sure, use the example timestamp provided in these instructions.
9. After replacing the number with a new timestamp it should look something like this:
    `"expires": 2150162047000,`
    - If the expires entry is the last thing before the closing `}` at the bottom there won't be a `,` afterwards
    - In all likelyhood though it's further near the top, meaning the `,` is required to separate this entry from the next one underneath
    - If in doubt, ask anyone for help who knows what `JSON` is :)
10. Click the `Apply` button just below the editable text field
11. Now click the `Write Changes` button at the top of the window to write a new version of the database file
12. Congrats, your session is now extended to whatever timestamp you put into the text file

Again, please read these steps once or twice in full before actually performing them.

This tool, instead of editing an existing session, will always create a new sesssion from scratch. It will have mostly the same structure, except most of the data are placeholder values, except for the important ones: Your user_id and the session expiry data.