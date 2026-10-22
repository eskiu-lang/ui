# Releasing to the stores

`ui release` builds an app for the App Store and Google Play:

```bash
ui release              # both iOS and Android (iOS only on a Mac)
ui release ios          # an IPA for App Store Connect
ui release android      # an App Bundle for Google Play, and an APK
ui release macos        # an ad-hoc signed .app and distributable .zip
ui release linux        # a release binary and distributable .tar.gz
ui release windows      # a release .exe and distributable .zip
ui release ios --upload # straight to App Store Connect, for TestFlight
```

It needs nothing set up first beyond the platforms' own tools (Xcode, the Android SDK, Gradle
9 and a JDK 17). It checks what the stores ask for before the long build, and ends with the
files it made, their size, the version and build number, and what to do next.

## Before the first release

In `ui.toml`:

```toml
[app]
name = "Tasks"
id = "com.yourcompany.tasks"   # the stores keep an app's id for good
icon = "icon.png"              # the App Store takes no app without one
version = "1.0.0"              # optional: the version people see
```

`ui release` warns when the id is still a placeholder (`org.eskiu.app.*`, `org.example.*`,
`com.example.*`) and stops an iOS release without an icon.

## The build number

Each upload to a store needs a build number larger than the last. Unless `[app] build` sets
one, a release takes the seconds since 2024-01-01, so it only grows and there is nothing to
bump. Builds that are not releases use 1.

## iOS

The release is built for devices (arm64) and archived by Xcode, which signs it for your team
and makes or fetches the certificate and provisioning profile it needs
(`-allowProvisioningUpdates`). The first time, sign in to your Apple developer account in
Xcode (Settings, Accounts).

The team is `[ios] team` in `ui.toml`, or the only team this Mac has a valid signing
certificate for. With several, `ui release ios` lists them, marking those with a
distribution certificate, and asks you to choose:

```toml
[ios]
team = "ABCDE12345"
```

The archive is exported as an IPA in `build/ui/ios/release/`, for App Store Connect. Send it
with `ui release ios --upload` (Xcode's account uploads it; it shows in TestFlight once Apple
has processed it) or with Apple's Transporter app. The app needs a record in App Store Connect
with the same bundle id before its first upload. `[ios] export` picks another export method,
such as `release-testing` (ad hoc) or `debugging`.

The archive stays in `build/ui/ios/xcode-release/`; Xcode's Organizer (Window, Organizer)
opens it too.

## Android

The release goes through Gradle (an App Bundle is Gradle's to make): `build/ui/android/release/`
gets `NAME.aab`, which Google Play takes, and `NAME.apk`, to install on a device directly. Both
are signed for release, not debuggable, and target the API level Google Play requires.

They are signed with the app's upload key. The first release makes one in
`~/.eskiu/keys/ID/` (`upload.jks`, and `upload.properties` with its random password), readable
only by you. Back up that folder: Play takes only uploads signed with this key (Google Play
re-signs the app with its own key, and Play Console, App integrity, can reset a lost upload
key). The Gradle build reads the password from that file; it is never written into the build.

On CI, or with a key of your own, set the environment instead:

```bash
UI_ANDROID_KEYSTORE=/path/to/upload.jks
UI_ANDROID_KEYSTORE_PASSWORD=...
UI_ANDROID_KEY_ALIAS=upload
UI_ANDROID_KEY_PASSWORD=...        # when it differs from the keystore's
```

Upload the `.aab` in Play Console (Test and release); the first one makes the app's record.

## Desktop

- **macOS**: `ui release macos` packages the standalone binary inside the `NAME.app` bundle with
  its `Info.plist` and `AppIcon.icns`, ad-hoc codesigns the bundle, and creates `NAME-VERSION-macos.zip`.
- **Linux**: `ui release linux` compiles the release binary and packages `NAME-VERSION-linux.tar.gz`.
- **Windows**: `ui release windows` compiles the release executable and packages `NAME-VERSION-windows.zip`.

## What is not there yet

- Uploading to Google Play from the command line (its API needs a service account).
- Android builds for x86_64 devices: releases carry arm64 code, which every phone on Google
  Play runs.
