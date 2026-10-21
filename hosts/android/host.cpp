#include <jni.h>
#include <yoga/Yoga.h>
#include "../commit.h"
#include "ui_native.h"
#include <string>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cstdlib>

extern "C" void ui_start(int);
extern "C" void ui_dispatch(int, int);
extern "C" void ui_dispatch_text(int, int, const char *);
extern "C" void ui_dispatch_focus(int, int, int);
extern "C" void ui_dispatch_toggle(int, int, int);
extern "C" void ui_dispatch_scroll(int, int, int, int);
extern "C" void ui_dispatch_alert(int, int);
extern "C" void ui_dispatch_refresh(int, int);
extern "C" int ui_report_back();
extern "C" void ui_report_link(const char *);
extern "C" void ui_report_window(int, int, int);
extern "C" int ui_edge_to_edge();
extern "C" void ui_report_insets(int, int, int, int, int);
extern "C" void ui_report_active(int);
extern "C" void ui_report_appearance(int);
extern "C" void ui_report_accessibility(int, int, int);
extern "C" void ui_report_locale(const char *, int);
extern "C" const char *ui_conformance_log();
extern "C" void ui_tick();
extern "C" void ui_stop();
extern "C" void ui_dispatch_native(const char *, const char *);
extern "C" void ui_dispatch_view(int, int, const char *, const char *);
extern "C" void ui_dispatch_layout(int, int, int, int, int, int, int, int);
extern "C" void ui_dispatch_timer(int);
extern "C" void ui_dispatch_hover(int, int, int);
extern "C" void ui_dispatch_pan(int, int, int, int, int, int, int);
extern "C" void ui_dispatch_value(int, int, int);
extern "C" void ui_dispatch_answer(int, const char *);
extern "C" void ui_report_data_dir(const char *);
extern "C" void ui_dispatch_fetch(int, int, const char *, const char *);

static JNIEnv *env;
static jobject activity;
static jmethodID applyMethod, measureMethod, placeMethod, widthMethod, heightMethod, scaleMethod;
static jmethodID scheduleMethod, cancelMethod, committedMethod, contentMethod, commandMethod, screenWidthMethod,
    screenHeightMethod;
static YGNodeRef root;
static std::unordered_map<int, YGNodeRef> nodes;
static std::unordered_map<int, int> scrolls;
// Modals: each its own layout root, laid out over the whole screen.
static std::vector<int> modals;
// Each node's generation, and the frames last reported for the nodes the app listens to
// (ui_on_layout): x, y, width, height in points, relative to the parent.
static std::unordered_map<int, int> generations;
static std::unordered_map<int, std::array<int, 6>> layouts;

// An edge offset that is not set (kUnset in src/core/state.esk).
static const int kUnset = -1000000;
static JavaVM *vm;
static jclass activityClass;
static jobject classLoader;
static jmethodID namedMethod, emitMethod, loadClassMethod, timerStartMethod, timerStopMethod, windowMethod;
static void checked() { if (env->ExceptionCheck()) { env->ExceptionDescribe(); std::abort(); } }
static YGSize measure(YGNodeConstRef node, float width, YGMeasureMode wm, float height, YGMeasureMode hm) {
    int id = static_cast<int>(reinterpret_cast<intptr_t>(YGNodeGetContext(node)));
    jlong size = env->CallLongMethod(activity, measureMethod, id, width, static_cast<int>(wm), height, static_cast<int>(hm));
    checked();
    float scale = env->CallFloatMethod(activity, scaleMethod);
    return {static_cast<float>(size >> 32) / scale, static_cast<float>(size & 0xffffffffLL) / scale};
}
static void place(YGNodeRef parent) {
    for (size_t i = 0; i < YGNodeGetChildCount(parent); i++) {
        YGNodeRef node = YGNodeGetChild(parent, i);
        // A hidden screen keeps its frame (it stays under a push's slide).
        if (YGNodeStyleGetDisplay(node) == YGDisplayNone) continue;
        int id = static_cast<int>(reinterpret_cast<intptr_t>(YGNodeGetContext(node)));
        env->CallVoidMethod(activity, placeMethod, id, YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node),
                            YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node));
        checked(); place(node);
        auto listening = layouts.find(id);
        if (listening != layouts.end()) {
            // Laid out now, with its ancestors: its window position is final.
            jlong window = env->CallLongMethod(activity, windowMethod, id);
            checked();
            std::array<int, 6> frame = {static_cast<int>(std::lround(YGNodeLayoutGetLeft(node))),
                                        static_cast<int>(std::lround(YGNodeLayoutGetTop(node))),
                                        static_cast<int>(std::lround(YGNodeLayoutGetWidth(node))),
                                        static_cast<int>(std::lround(YGNodeLayoutGetHeight(node))),
                                        static_cast<int>(window >> 32), static_cast<int>(window & 0xffffffff)};
            if (frame != listening->second) {
                listening->second = frame;
                ui_dispatch_layout(id, generations[id], frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]);
            }
        }
        // A scroll view's content is as tall as its lowest child.
        if (scrolls.count(id)) {
            float bottom = 0;
            for (size_t j = 0; j < YGNodeGetChildCount(node); j++) {
                YGNodeRef child = YGNodeGetChild(node, j);
                bottom = std::max(bottom, YGNodeLayoutGetTop(child) + YGNodeLayoutGetHeight(child));
            }
            env->CallVoidMethod(activity, contentMethod, id, YGNodeLayoutGetWidth(node),
                                bottom + YGNodeLayoutGetPadding(node, YGEdgeBottom));
            checked();
        }
    }
}
extern "C" void ui_host_schedule() { env->CallVoidMethod(activity, scheduleMethod); checked(); }
extern "C" void ui_host_cancel() { env->CallVoidMethod(activity, cancelMethod); checked(); }
extern "C" void ui_host_timer_start(int token, int ms, int repeat) {
    env->CallVoidMethod(activity, timerStartMethod, token, ms, repeat != 0); checked();
}
extern "C" void ui_host_timer_stop(int token) { env->CallVoidMethod(activity, timerStopMethod, token); checked(); }
extern "C" void ui_host_apply(int op, int id, int generation, int parent, int before, int kind, const char *text) {
    if (op == 1) {
        YGNodeRef node = YGNodeNew();
        nodes[id] = node;
        YGNodeSetContext(node, reinterpret_cast<void *>(static_cast<intptr_t>(id)));
        // Measured leaves: text, button, input, image, switch, activity indicator.
        bool measured = kind == 3 || kind == 4 || kind == 5 || kind == 7 || kind == 9 || kind == 10 || kind == 15;
        if (measured) YGNodeSetMeasureFunc(node, measure);
        else YGNodeStyleSetFlexDirection(node, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
        // A scroll shrinks to the space its parent has, as React Native's ScrollView.
        if (kind == 6) { YGNodeStyleSetOverflow(node, YGOverflowScroll); YGNodeStyleSetFlexShrink(node, 1); scrolls[id] = 1; }
        if (kind == 11) YGNodeStyleSetDisplay(node, YGDisplayNone);
        if (kind == 12) modals.push_back(id);
        generations[id] = generation;
    } else if (nodes.count(id)) {
        YGNodeRef node = nodes.at(id);
        // Props: the code in kind (1 gap, 2 padding), the value in before.
        if (op == 2) {
            if (kind == 1) YGNodeStyleSetGap(node, YGGutterAll, before);
            // A measured view (an EditText) applies its own padding: Yoga's would count twice.
            else if (kind == 2) YGNodeStyleSetPadding(node, YGEdgeAll, YGNodeHasMeasureFunc(node) ? 0 : before);
            else if (kind == 4) { if (before) YGNodeStyleSetWidth(node, before); else YGNodeStyleSetWidthAuto(node); }
            else if (kind == 5) { if (before) YGNodeStyleSetHeight(node, before); else YGNodeStyleSetHeightAuto(node); }
            else if (kind == 6) {
                // A growing child also shrinks to its parent, so a scroll under it stays in the window.
                YGNodeStyleSetFlexGrow(node, before);
                if (!scrolls.count(id)) YGNodeStyleSetFlexShrink(node, before > 0 ? 1 : 0);
            }
            else if (kind == 7) YGNodeStyleSetFlexWrap(node, before ? YGWrapWrap : YGWrapNoWrap);
            else if ((kind == 10 || kind == 11) && YGNodeHasMeasureFunc(node)) YGNodeMarkDirty(node);
            else if (kind == 12) YGNodeStyleSetBorder(node, YGEdgeAll, before);
            else if (kind == 23) YGNodeStyleSetDisplay(node, before ? YGDisplayFlex : YGDisplayNone);
            else if (kind == 30) { if (before) layouts[id] = {-1, -1, -1, -1, -1, -1}; else layouts.erase(id); }
            else if (kind == 42) YGNodeStyleSetMinWidth(node, before);
            else if (kind == 43) YGNodeStyleSetMaxWidth(node, before ? static_cast<float>(before) : YGUndefined);
            else if (kind == 44) YGNodeStyleSetMinHeight(node, before);
            else if (kind == 45) YGNodeStyleSetMaxHeight(node, before ? static_cast<float>(before) : YGUndefined);
            else if (kind == 24) YGNodeStyleSetPositionType(node, before ? YGPositionTypeAbsolute : YGPositionTypeRelative);
            else if (kind >= 25 && kind <= 28) {
                const YGEdge edges[] = {YGEdgeLeft, YGEdgeTop, YGEdgeRight, YGEdgeBottom};
                YGNodeStyleSetPosition(node, edges[kind - 25], before == kUnset ? YGUndefined : static_cast<float>(before));
            } else if (kind == 29) YGNodeStyleSetAspectRatio(node, before ? before / 1000.0f : YGUndefined);
            else if (kind == 16) {
                const YGAlign aligns[] = {YGAlignStretch, YGAlignFlexStart, YGAlignCenter, YGAlignFlexEnd, YGAlignStretch};
                YGNodeStyleSetAlignItems(node, aligns[std::min(4, std::max(0, before))]);
            } else if (kind == 17) {
                const YGJustify justifies[] = {YGJustifyFlexStart, YGJustifyFlexStart, YGJustifyCenter, YGJustifyFlexEnd,
                                               YGJustifySpaceBetween, YGJustifySpaceAround};
                YGNodeStyleSetJustifyContent(node, justifies[std::min(5, std::max(0, before))]);
            }
        }
        else if (op == 3) YGNodeMarkDirty(node);
        else if ((op == 4 || op == 5) && std::find(modals.begin(), modals.end(), id) != modals.end()) {
            // A modal stays out of its parent's layout.
        } else if (op == 4 || op == 5) {
            YGNodeRef owner = nodes.count(parent) ? nodes.at(parent) : root;
            if (YGNodeGetParent(node)) YGNodeRemoveChild(YGNodeGetParent(node), node);
            size_t index = 0;
            while (index < YGNodeGetChildCount(owner) && (!before || YGNodeGetChild(owner, index) != nodes.at(before))) index++;
            YGNodeInsertChild(owner, node, index);
        } else if (op == 6) {
            if (YGNodeGetParent(node)) YGNodeRemoveChild(YGNodeGetParent(node), node);
            YGNodeFree(node); nodes.erase(id); scrolls.erase(id); generations.erase(id); layouts.erase(id);
            modals.erase(std::remove(modals.begin(), modals.end(), id), modals.end());
        }
    }
    jstring value = text ? env->NewStringUTF(text) : nullptr;
    env->CallVoidMethod(activity, applyMethod, op, id, generation, parent, before, kind, value);
    if (value) env->DeleteLocalRef(value);
    checked();
}
extern "C" void ui_host_ready();
// Commands (alerts, URLs) go to the activity with their strings.
static void ui_host_command(int request, int command, int count, const char **strings) {
    jobjectArray values = env->NewObjectArray(count, env->FindClass("java/lang/String"), nullptr);
    for (int i = 0; i < count; i++) {
        jstring value = env->NewStringUTF(strings[i]);
        env->SetObjectArrayElement(values, i, value);
        env->DeleteLocalRef(value);
    }
    env->CallVoidMethod(activity, commandMethod, request, command, values);
    env->DeleteLocalRef(values);
    checked();
}
static void layoutModals() {
    float width = env->CallFloatMethod(activity, screenWidthMethod), height = env->CallFloatMethod(activity, screenHeightMethod);
    checked();
    for (int id : modals) { YGNodeCalculateLayout(nodes.at(id), width, height, YGDirectionLTR); place(nodes.at(id)); }
}
static void ui_host_named(int id, const char *name, const char *value) {
    jstring n = env->NewStringUTF(name), v = env->NewStringUTF(value);
    env->CallVoidMethod(activity, namedMethod, id, n, v);
    env->DeleteLocalRef(n); env->DeleteLocalRef(v);
    checked();
}
extern "C" void ui_host_commit(const uint8_t *data, int len) {
    ui_commit_each(data, len, ui_host_apply, ui_host_command, ui_host_named);
    ui_host_ready();
}
extern "C" void ui_host_ready() {
    YGNodeCalculateLayout(root, env->CallFloatMethod(activity, widthMethod), env->CallFloatMethod(activity, heightMethod), YGDirectionLTR);
    checked(); place(root);
    layoutModals();
    env->CallVoidMethod(activity, committedMethod); checked();
}

extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeStart(JNIEnv *current, jobject self, jint example) {
    env = current;
    // A second start (the self-test's TODO) reuses the activity and the root.
    if (activity) { ui_start(example); return; }
    activity = env->NewGlobalRef(self);
    jclass cls = env->GetObjectClass(self);
    applyMethod = env->GetMethodID(cls, "apply", "(IIIIIILjava/lang/String;)V");
    measureMethod = env->GetMethodID(cls, "measure", "(IFIFI)J");
    placeMethod = env->GetMethodID(cls, "place", "(IFFFF)V");
    widthMethod = env->GetMethodID(cls, "width", "()F"); heightMethod = env->GetMethodID(cls, "height", "()F");
    scaleMethod = env->GetMethodID(cls, "scale", "()F");
    scheduleMethod = env->GetMethodID(cls, "schedule", "()V"); cancelMethod = env->GetMethodID(cls, "cancel", "()V");
    committedMethod = env->GetMethodID(cls, "committed", "()V");
    contentMethod = env->GetMethodID(cls, "content", "(IFF)V");
    commandMethod = env->GetMethodID(cls, "command", "(II[Ljava/lang/String;)V");
    screenWidthMethod = env->GetMethodID(cls, "screenWidth", "()F");
    screenHeightMethod = env->GetMethodID(cls, "screenHeight", "()F");
    namedMethod = env->GetMethodID(cls, "named", "(ILjava/lang/String;Ljava/lang/String;)V");
    timerStartMethod = env->GetMethodID(cls, "timerStart", "(IIZ)V");
    timerStopMethod = env->GetMethodID(cls, "timerStop", "(I)V");
    windowMethod = env->GetMethodID(cls, "windowOf", "(I)J");
    emitMethod = env->GetStaticMethodID(cls, "emit", "(Ljava/lang/String;Ljava/lang/String;)V");
    activityClass = (jclass)env->NewGlobalRef(cls);
    // The app's class loader, for ui_find_class on threads native code starts.
    jclass classClass = env->FindClass("java/lang/Class");
    jobject loader = env->CallObjectMethod(cls, env->GetMethodID(classClass, "getClassLoader", "()Ljava/lang/ClassLoader;"));
    classLoader = env->NewGlobalRef(loader);
    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    loadClassMethod = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    env->DeleteLocalRef(loader); env->DeleteLocalRef(loaderClass); env->DeleteLocalRef(classClass);
    env->DeleteLocalRef(cls); checked();
    root = YGNodeNew();
    YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
    ui_start(example);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeEvent(JNIEnv *current, jobject, jint id, jint generation) {
    env = current; ui_dispatch(id, generation);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeText(JNIEnv *current, jobject, jint id, jint generation, jstring text) {
    env = current;
    const char *chars = env->GetStringUTFChars(text, nullptr);
    ui_dispatch_text(id, generation, chars);
    env->ReleaseStringUTFChars(text, chars);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeFocus(JNIEnv *current, jobject, jint id, jint generation, jint focused) {
    env = current; ui_dispatch_focus(id, generation, focused);
}
extern "C" JNIEXPORT jstring JNICALL Java_org_eskiu_ui_UiActivity_nativeConformanceLog(JNIEnv *current, jobject) {
    return current->NewStringUTF(ui_conformance_log());
}
extern "C" JNIEXPORT jint JNICALL Java_org_eskiu_ui_UiActivity_nativeEdgeToEdge(JNIEnv *, jobject) {
    return ui_edge_to_edge();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeInsets(JNIEnv *current, jobject, jint top, jint right, jint bottom, jint left, jint keyboard) {
    env = current; ui_report_insets(top, right, bottom, left, keyboard);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeActive(JNIEnv *current, jobject, jint value) {
    env = current; ui_report_active(value);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeScroll(JNIEnv *current, jobject, jint id, jint generation, jint offset, jint viewport) {
    env = current; ui_dispatch_scroll(id, generation, offset, viewport);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeToggle(JNIEnv *current, jobject, jint id, jint generation, jint on) {
    env = current; ui_dispatch_toggle(id, generation, on);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeWindow(JNIEnv *current, jobject, jint width, jint height, jint scale) {
    env = current; ui_report_window(width, height, scale);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeTick(JNIEnv *current, jobject) { env = current; ui_tick(); }
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeLayout(JNIEnv *current, jobject, jfloat width, jfloat height) {
    env = current; YGNodeCalculateLayout(root, width, height, YGDirectionLTR); place(root); layoutModals();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeRefresh(JNIEnv *current, jobject, jint id, jint generation) {
    env = current; ui_dispatch_refresh(id, generation);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeAlert(JNIEnv *current, jobject, jint request, jint choice) {
    env = current; ui_dispatch_alert(request, choice);
}
extern "C" JNIEXPORT jint JNICALL Java_org_eskiu_ui_UiActivity_nativeBack(JNIEnv *current, jobject) {
    env = current; return ui_report_back();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeLink(JNIEnv *current, jobject, jstring url) {
    env = current;
    const char *chars = env->GetStringUTFChars(url, nullptr);
    ui_report_link(chars);
    env->ReleaseStringUTFChars(url, chars);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeStop(JNIEnv *current, jobject) {
    env = current; ui_stop(); YGNodeFree(root); env->DeleteGlobalRef(activity); activity = nullptr;
}

// Native code: events from Java (UiActivity.emit, a view's events) and the C API in
// ui_native.h.
extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *loaded, void *) { vm = loaded; return JNI_VERSION_1_6; }
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeModuleEvent(JNIEnv *current, jobject, jstring channel, jstring payload) {
    env = current;
    const char *c = env->GetStringUTFChars(channel, nullptr), *p = env->GetStringUTFChars(payload, nullptr);
    ui_dispatch_native(c, p);
    env->ReleaseStringUTFChars(channel, c); env->ReleaseStringUTFChars(payload, p);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeViewEvent(JNIEnv *current, jobject, jint id, jint generation, jstring name, jstring payload) {
    env = current;
    const char *n = env->GetStringUTFChars(name, nullptr), *p = env->GetStringUTFChars(payload, nullptr);
    ui_dispatch_view(id, generation, n, p);
    env->ReleaseStringUTFChars(name, n); env->ReleaseStringUTFChars(payload, p);
}
extern "C" JNIEnv *ui_jni_env(void) {
    JNIEnv *current = nullptr;
    if (vm->GetEnv(reinterpret_cast<void **>(&current), JNI_VERSION_1_6) == JNI_EDETACHED) vm->AttachCurrentThread(&current, nullptr);
    return current;
}
extern "C" jobject ui_activity(void) { return activity; }
extern "C" jclass ui_find_class(const char *name) {
    JNIEnv *current = ui_jni_env();
    std::string dotted(name);
    std::replace(dotted.begin(), dotted.end(), '/', '.');
    jstring text = current->NewStringUTF(dotted.c_str());
    jclass found = (jclass)current->CallObjectMethod(classLoader, loadClassMethod, text);
    current->DeleteLocalRef(text);
    if (current->ExceptionCheck()) { current->ExceptionClear(); return nullptr; }
    return found;
}
extern "C" void ui_native_emit(const char *channel, const char *payload) {
    JNIEnv *current = ui_jni_env();
    jstring c = current->NewStringUTF(channel ? channel : ""), p = current->NewStringUTF(payload ? payload : "");
    current->CallStaticVoidMethod(activityClass, emitMethod, c, p);
    current->DeleteLocalRef(c); current->DeleteLocalRef(p);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeTimer(JNIEnv *current, jobject, jint token) {
    env = current; ui_dispatch_timer(token);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeHover(JNIEnv *current, jobject, jint id, jint generation, jint inside) {
    env = current; ui_dispatch_hover(id, generation, inside);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeValue(JNIEnv *current, jobject, jint id, jint generation, jint value) {
    env = current; ui_dispatch_value(id, generation, value);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeSystem(JNIEnv *current, jobject, jint dark, jint reader, jint still, jint scale, jstring locale, jint rtl) {
    env = current;
    ui_report_appearance(dark);
    ui_report_accessibility(reader, still, scale);
    const char *tag = env->GetStringUTFChars(locale, nullptr);
    ui_report_locale(tag, rtl);
    env->ReleaseStringUTFChars(locale, tag);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeAnswer(JNIEnv *current, jobject, jint request, jstring text) {
    env = current;
    const char *value = env->GetStringUTFChars(text, nullptr);
    ui_dispatch_answer(request, value);
    env->ReleaseStringUTFChars(text, value);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeDataDir(JNIEnv *current, jobject, jstring path) {
    const char *value = current->GetStringUTFChars(path, nullptr);
    ui_report_data_dir(value);
    current->ReleaseStringUTFChars(path, value);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeFetch(JNIEnv *current, jobject, jint request, jint status, jstring body, jstring headers) {
    env = current;
    const char *text = env->GetStringUTFChars(body, nullptr), *names = env->GetStringUTFChars(headers, nullptr);
    ui_dispatch_fetch(request, status, text, names);
    env->ReleaseStringUTFChars(body, text); env->ReleaseStringUTFChars(headers, names);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativePan(JNIEnv *current, jobject, jint id, jint generation, jint phase, jint dx, jint dy, jint vx, jint vy) {
    env = current; ui_dispatch_pan(id, generation, phase, dx, dy, vx, vy);
}
// Scrolling moves what is inside in the window without a layout: the listened nodes' window
// positions are checked again.
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_UiActivity_nativeScrolled(JNIEnv *current, jobject) {
    env = current;
    for (auto &entry : layouts) {
        if (entry.second[2] < 0) continue;
        jlong window = env->CallLongMethod(activity, windowMethod, entry.first);
        checked();
        int x = static_cast<int>(window >> 32), y = static_cast<int>(window & 0xffffffff);
        if (x == entry.second[4] && y == entry.second[5]) continue;
        entry.second[4] = x; entry.second[5] = y;
        ui_dispatch_layout(entry.first, generations[entry.first], entry.second[0], entry.second[1], entry.second[2], entry.second[3], x, y);
    }
}
