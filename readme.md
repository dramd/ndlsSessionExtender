# NDLS Session Extender

*This tool and all code within is not affiliated with or endorsed by Endlesss Ltd. Use at your own risk.*

## The problem

As of writing this, the expectation is for the Endlesss servers to shut down sometime around May 31st 2024. From that point onwards any login attempt from the Endlesss application will fail. Sadly, the way the application works is that it expects to have a valid session for your account in order to let you past the login screen, whether you're connected to the internet or not. This makes even the solo jam inaccessible.

## The partial fix

The code provided by this repository can extend an account's session (as far as the Endlesss application is concerned) for an indefinite period of time. This doesn't make the application usable online once the servers are shut off, but it should be possible to retain access to your solo jam and use the application in that limited scope.

In its current state, **this requires you to be logged into your Endlesss account whose solo jam you want to make accessible in the future before the servers are shut down**. Though it is likely possible to make this fix work even if you're not logged in, or if want to switch between multiple accounts (locally!), the current iteration of this fix doesn't yet support or promise this.

Once again in short:
- This fix doesn't magically make any of the online features of Endlesss work. No servers = no service
- You should be able to still use _one_ solo jam from _one_ account whose session is extended, so that the application will let you through the door
- Make sure you are logged into your Endlesss account from within the Endlesss application before the servers are shut down
- This is for the desktop client only! It cannot be done manually or using this tool on iOS.

## How it works

You do not have to use the tool in this repository in order to apply the fix. It can be done easily in just a few steps. The tool simply automates that process and does nothing more. These are the steps to apply the fix manually. Read these steps once or twice all the way through before performing them! It is also advisable to make a backup copy of the `global.cblite2` file you will be editing.

1. Download the [https://sqlitebrowser.org/dl/](DB Browser for SQLite) and install it
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

## What this tool does

The code in this repository automates the above steps by replacing the expiry timestamp with a value equivalent to roughly the year 2038. It will try to automatically find the `global.cblite2` file to edit and otherwise ask you to browse for it. It will then perform the necessary database edit that has the same effect as the steps above.

## Some examples

This is a heavily reduced version of the data you'll find in the active session document in the database. The real document will have more entries than this:

```
{
    "app_version": 7339,
    "created": 1716814923764,
    "expires": 2150162047000,
    "issued": 1716814924600,
    "password": "a0dij2dai02diajd2akda0d",
    "provider": "local",
    "type": "Session",
    "user_id": "myaccountname"
}
```

- `user_id` is the account you are still logged into
- `expires` is the timestamp that should be in the far future to retain access
In this example, the timestamp has already been extended. In an unmodified file, the value will be much closer to that associated with `created` or `issued`.