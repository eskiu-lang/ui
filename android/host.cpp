#include <jni.h>
#include <yoga/Yoga.h>
#include "../hosts/ui_commit.h"
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include <cstdlib>

extern "C" void ui_start(int);
extern "C" void ui_dispatch(int, int);
extern "C" void ui_dispatch_text(int, int, const char *);
extern "C" void ui_dispatch_focus(int, int, int);
extern "C" void ui_tick();
extern "C" void ui_stop();

static JNIEnv *env;
static jobject activity;
static jmethodID applyMethod, measureMethod, placeMethod, widthMethod, heightMethod, scaleMethod;
static jmethodID scheduleMethod, cancelMethod, committedMethod, contentMethod;
static YGNodeRef root;
static std::unordered_map<int, YGNodeRef> nodes;
static std::unordered_map<int, int> scrolls;

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
        if (kind >= 3 && kind != 6) YGNodeSetMeasureFunc(node, measure);
        else YGNodeStyleSetFlexDirection(node, kind == 2 ? YGFlexDirectionRow : YGFlexDirectionColumn);
        if (kind == 6) { YGNodeStyleSetOverflow(node, YGOverflowScroll); scrolls[id] = 1; }
    } else if (nodes.count(id)) {
        YGNodeRef node = nodes.at(id);
        // Props: the code in kind (1 gap, 2 padding), the value in before.
        if (op == 2) {
            if (kind == 1) YGNodeStyleSetGap(node, YGGutterAll, before);
            else if (kind == 2) YGNodeStyleSetPadding(node, YGEdgeAll, before);
            else if (kind == 4) { if (before) YGNodeStyleSetWidth(node, before); else YGNodeStyleSetWidthAuto(node); }
            else if (kind == 5) { if (before) YGNodeStyleSetHeight(node, before); else YGNodeStyleSetHeightAuto(node); }
            else if (kind == 6) YGNodeStyleSetFlexGrow(node, before);
        }
        else if (op == 3) YGNodeMarkDirty(node);
        else if (op == 4 || op == 5) {
            YGNodeRef owner = nodes.count(parent) ? nodes.at(parent) : root;
            if (YGNodeGetParent(node)) YGNodeRemoveChild(YGNodeGetParent(node), node);
            size_t index = 0;
            while (index < YGNodeGetChildCount(owner) && (!before || YGNodeGetChild(owner, index) != nodes.at(before))) index++;
            YGNodeInsertChild(owner, node, index);
        } else if (op == 6) {
            if (YGNodeGetParent(node)) YGNodeRemoveChild(YGNodeGetParent(node), node);
            YGNodeFree(node); nodes.erase(id); scrolls.erase(id);
        }
    }
    jstring value = text ? env->NewStringUTF(text) : nullptr;
    env->CallVoidMethod(activity, applyMethod, op, id, generation, parent, before, kind, value);
    if (value) env->DeleteLocalRef(value);
    checked();
}
extern "C" void ui_host_ready();
extern "C" void ui_host_commit(const uint8_t *data, int len) {
    ui_commit_each(data, len, ui_host_apply);
    ui_host_ready();
}
extern "C" void ui_host_ready() {
    YGNodeCalculateLayout(root, env->CallFloatMethod(activity, widthMethod), env->CallFloatMethod(activity, heightMethod), YGDirectionLTR);
    checked(); place(root);
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
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeTick(JNIEnv *current, jobject) { env = current; ui_tick(); }
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeLayout(JNIEnv *current, jobject, jfloat width, jfloat height) {
    env = current; YGNodeCalculateLayout(root, width, height, YGDirectionLTR); place(root);
}
extern "C" JNIEXPORT void JNICALL Java_org_eskiu_ui_prototype_UiActivity_nativeStop(JNIEnv *current, jobject) {
    env = current; ui_stop(); YGNodeFree(root); env->DeleteGlobalRef(activity); activity = nullptr;
}
