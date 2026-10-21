// Native C and C++ code for an Eskiu ui app on Android (native/android/*.c and *.cpp, built
// into the app's library by `ui build android`). Views and Java modules go in
// native/android/*.java, through org.eskiu.ui.UiActivity's registerView and emit.
#ifndef UI_NATIVE_H
#define UI_NATIVE_H
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

// Post `payload` on `channel` to the app's use_native_event handlers. Callable from any
// thread: the event reaches Eskiu on the UI thread.
void ui_native_emit(const char *channel, const char *payload);

// This thread's JNIEnv (attached on first use).
JNIEnv *ui_jni_env(void);

// An app class by its binary name ("com/example/Battery"), through the app's class loader,
// which JNI's FindClass does not use on a thread native code started. A local reference.
jclass ui_find_class(const char *name);

// The activity (a global reference), for a Context.
jobject ui_activity(void);

#ifdef __cplusplus
}
#endif

#endif
