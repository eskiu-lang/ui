#include <jni.h>
#include <yoga/Yoga.h>
#include "../hosts/ui_commit.h"
#include <algorithm>
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
extern "C" const char *ui_conformance_log();
extern "C" void ui_tick();
extern "C" void ui_stop();

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
        int id = static_cast<int>(reinterpret_cast<intptr_t>(YGNodeGetContext(node)));
        env->CallVoidMethod(activity, placeMethod, id, YGNodeLayoutGetLeft(node), YGNodeLayoutGetTop(node),
                            YGNodeLayoutGetWidth(node), YGNodeLayoutGetHeight(node));
        checked(); place(node);
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
extern "C" void ui_host_apply(int op, int id, int generation, int parent, int before, int kind, const char *text) {
    if (op == 1) {
        YGNodeRef node = YGNodeNew();
        nodes[id] = node;
        YGNodeSetContext(node, reinterpret_cast<void *>(static_cast<intptr_t>(id)));
        // Measured leaves: text, button, input, image, switch, activity indicator.
        bool measured = kind == 3 || kind == 4 || kind == 5 || kind == 7 || kind == 9 || kind == 10;
        if (measured) YGNodeSetMeasureFunc(node, measure);
        else YGNodeStyleSetFlexDirection(node, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
        if (kind == 6) { YGNodeStyleSetOverflow(node, YGOverflowScroll); scrolls[id] = 1; }
        if (kind == 11) YGNodeStyleSetDisplay(node, YGDisplayNone);
        if (kind == 12) modals.push_back(id);
    } else if (nodes.count(id)) {
        YGNodeRef node = nodes.at(id);
        // Props: the code in kind (1 gap, 2 padding), the value in before.
        if (op == 2) {
            if (kind == 1) YGNodeStyleSetGap(node, YGGutterAll, before);
            else if (kind == 2) YGNodeStyleSetPadding(node, YGEdgeAll, before);
            else if (kind == 4) { if (before) YGNodeStyleSetWidth(node, before); else YGNodeStyleSetWidthAuto(node); }
            else if (kind == 5) { if (before) YGNodeStyleSetHeight(node, before); else YGNodeStyleSetHeightAuto(node); }
            else if (kind == 6) YGNodeStyleSetFlexGrow(node, before);
            else if (kind == 7) YGNodeStyleSetFlexWrap(node, before ? YGWrapWrap : YGWrapNoWrap);
            else if ((kind == 10 || kind == 11) && YGNodeHasMeasureFunc(node)) YGNodeMarkDirty(node);
            else if (kind == 12) YGNodeStyleSetBorder(node, YGEdgeAll, before);
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
            YGNodeFree(node); nodes.erase(id); scrolls.erase(id);
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
extern "C" void ui_host_commit(const uint8_t *data, int len) {
    ui_commit_each(data, len, ui_host_apply, ui_host_command);
    ui_host_ready();
}
extern "C" void ui_host_ready() {
    YGNodeCalculateLayout(root, env->CallFloatMethod(activity, widthMethod), env->CallFloatMethod(activity, heightMethod), YGDirectionLTR);
    checked(); place(root);
    layoutModals();
    env->CallVoidMethod(activity, committedMethod); checked();
}

extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeStart(JNIEnv *current, jobject self, jint example) {
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
    env->DeleteLocalRef(cls); checked();
    root = YGNodeNew();
    YGNodeStyleSetFlexDirection(root, YGFlexDirectionColumn);
    ui_start(example);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeEvent(JNIEnv *current, jobject, jint id, jint generation) {
    env = current; ui_dispatch(id, generation);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeText(JNIEnv *current, jobject, jint id, jint generation, jstring text) {
    env = current;
    const char *chars = env->GetStringUTFChars(text, nullptr);
    ui_dispatch_text(id, generation, chars);
    env->ReleaseStringUTFChars(text, chars);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeFocus(JNIEnv *current, jobject, jint id, jint generation, jint focused) {
    env = current; ui_dispatch_focus(id, generation, focused);
}
extern "C" JNIEXPORT jstring JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeConformanceLog(JNIEnv *current, jobject) {
    return current->NewStringUTF(ui_conformance_log());
}
extern "C" JNIEXPORT jint JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeEdgeToEdge(JNIEnv *, jobject) {
    return ui_edge_to_edge();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeInsets(JNIEnv *current, jobject, jint top, jint right, jint bottom, jint left, jint keyboard) {
    env = current; ui_report_insets(top, right, bottom, left, keyboard);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeActive(JNIEnv *current, jobject, jint value) {
    env = current; ui_report_active(value);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeScroll(JNIEnv *current, jobject, jint id, jint generation, jint offset, jint viewport) {
    env = current; ui_dispatch_scroll(id, generation, offset, viewport);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeToggle(JNIEnv *current, jobject, jint id, jint generation, jint on) {
    env = current; ui_dispatch_toggle(id, generation, on);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeWindow(JNIEnv *current, jobject, jint width, jint height, jint scale) {
    env = current; ui_report_window(width, height, scale);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeTick(JNIEnv *current, jobject) { env = current; ui_tick(); }
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeLayout(JNIEnv *current, jobject, jfloat width, jfloat height) {
    env = current; YGNodeCalculateLayout(root, width, height, YGDirectionLTR); place(root); layoutModals();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeRefresh(JNIEnv *current, jobject, jint id, jint generation) {
    env = current; ui_dispatch_refresh(id, generation);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeAlert(JNIEnv *current, jobject, jint request, jint choice) {
    env = current; ui_dispatch_alert(request, choice);
}
extern "C" JNIEXPORT jint JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeBack(JNIEnv *current, jobject) {
    env = current; return ui_report_back();
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeLink(JNIEnv *current, jobject, jstring url) {
    env = current;
    const char *chars = env->GetStringUTFChars(url, nullptr);
    ui_report_link(chars);
    env->ReleaseStringUTFChars(url, chars);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeStop(JNIEnv *current, jobject) {
    env = current; ui_stop(); YGNodeFree(root); env->DeleteGlobalRef(activity); activity = nullptr;
}
