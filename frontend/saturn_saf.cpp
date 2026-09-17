#include "saturn_saf.h"

#include <SDL3/SDL.h>

#if defined(__ANDROID__)
#include <jni.h>

namespace saturn {
namespace {

/*
 * Calling a Kotlin object from C++.
 *
 * SafBridge is a Kotlin `object`, so its @JvmStatic methods are genuinely
 * static on the class -- there is no instance to find and no INSTANCE field to
 * read. That keeps this to: find the class once, look up the method, call it.
 *
 * The JNIEnv is fetched every time rather than cached. It belongs to the
 * calling thread, and these are called from both the emulator thread and the
 * UI thread; a cached one would be right until the day it was not.
 */
jclass bridge_class(JNIEnv *env)
{
    static jclass cached = nullptr;
    if (cached) return cached;
    jclass local = env->FindClass("com/crownpark/retro_saturn/SafBridge");
    if (!local) { env->ExceptionClear(); return nullptr; }
    cached = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    return cached;
}

std::string to_std(JNIEnv *env, jstring s)
{
    if (!s) return std::string();
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

/* Call a static method returning String, with up to four String arguments. */
std::string call_str(const char *name, const char *sig,
                     const std::vector<std::string> &args)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return std::string();
    jclass cls = bridge_class(env);
    if (!cls) return std::string();
    jmethodID m = env->GetStaticMethodID(cls, name, sig);
    if (!m) { env->ExceptionClear(); return std::string(); }

    jvalue jargs[4] = {};
    jstring locals[4] = {};
    for (size_t i = 0; i < args.size() && i < 4; ++i) {
        locals[i] = env->NewStringUTF(args[i].c_str());
        jargs[i].l = locals[i];
    }
    jstring r = (jstring)env->CallStaticObjectMethodA(cls, m, jargs);
    if (env->ExceptionCheck()) { env->ExceptionClear(); r = nullptr; }
    std::string out = to_std(env, r);
    if (r) env->DeleteLocalRef(r);
    for (size_t i = 0; i < args.size() && i < 4; ++i)
        if (locals[i]) env->DeleteLocalRef(locals[i]);
    return out;
}

void call_void(const char *name, const char *sig,
               const std::vector<std::string> &args)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return;
    jclass cls = bridge_class(env);
    if (!cls) return;
    jmethodID m = env->GetStaticMethodID(cls, name, sig);
    if (!m) { env->ExceptionClear(); return; }
    jvalue jargs[4] = {};
    jstring locals[4] = {};
    for (size_t i = 0; i < args.size() && i < 4; ++i) {
        locals[i] = env->NewStringUTF(args[i].c_str());
        jargs[i].l = locals[i];
    }
    env->CallStaticVoidMethodA(cls, m, jargs);
    if (env->ExceptionCheck()) env->ExceptionClear();
    for (size_t i = 0; i < args.size() && i < 4; ++i)
        if (locals[i]) env->DeleteLocalRef(locals[i]);
}

int call_int(const char *name)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return -1;
    jclass cls = bridge_class(env);
    if (!cls) return -1;
    jmethodID m = env->GetStaticMethodID(cls, name, "()I");
    if (!m) { env->ExceptionClear(); return -1; }
    const jint v = env->CallStaticIntMethod(cls, m);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
    return (int)v;
}

/* The bridge answers in lines of tab-separated fields, which keeps the JNI to
 * strings and primitives and needs no array marshalling on either side. */
std::vector<std::vector<std::string>> rows(const std::string &text, int fields)
{
    std::vector<std::vector<std::string>> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        const std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        std::vector<std::string> row;
        size_t p = 0;
        for (int i = 0; i < fields; ++i) {
            size_t tab = (i == fields - 1) ? std::string::npos : line.find('\t', p);
            row.push_back(line.substr(p, tab == std::string::npos
                                          ? std::string::npos : tab - p));
            if (tab == std::string::npos) break;
            p = tab + 1;
        }
        out.push_back(std::move(row));
    }
    return out;
}

} /* namespace */

bool saf_available() { return true; }

void saf_pick() { call_void("pick", "()V", {}); }

void saf_forget(const std::string &uri)
{
    call_void("forget", "(Ljava/lang/String;)V", { uri });
}

bool saf_ensure_layout(const std::string &uri)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return false;
    jclass cls = bridge_class(env);
    if (!cls) return false;
    jmethodID m = env->GetStaticMethodID(cls, "ensureLayout", "(Ljava/lang/String;)Z");
    if (!m) { env->ExceptionClear(); return false; }
    jstring s = env->NewStringUTF(uri.c_str());
    const jboolean ok = env->CallStaticBooleanMethod(cls, m, s);
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->DeleteLocalRef(s);
    return ok == JNI_TRUE;
}

std::vector<SafTree> saf_trees()
{
    std::vector<SafTree> out;
    for (auto &r : rows(call_str("trees", "()Ljava/lang/String;", {}), 2)) {
        if (r.size() < 2) continue;
        SafTree t;
        t.uri  = r[0];
        t.name = r[1];
        t.path = call_str("realPath", "(Ljava/lang/String;)Ljava/lang/String;", { t.uri });
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<SafEntry> saf_list(const std::string &uri, const std::string &sub)
{
    std::vector<SafEntry> out;
    const std::string text = call_str(
        "list", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", { uri, sub });
    for (auto &r : rows(text, 2)) {
        if (r.size() < 2) continue;
        SafEntry e;
        e.name  = r[0];
        e.bytes = SDL_strtoll(r[1].c_str(), nullptr, 10);
        out.push_back(std::move(e));
    }
    return out;
}

std::string saf_stage(const std::string &uri, const std::string &sub,
                      const std::string &name, const std::string &dest_dir)
{
    return call_str("stage",
                    "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;"
                    "Ljava/lang/String;)Ljava/lang/String;",
                    { uri, sub, name, dest_dir });
}

int saf_stage_progress() { return call_int("stageProgress"); }

std::string saf_folder_name(const std::string &uri, const std::string &kind)
{
    const std::string n = call_str(
        "folderName", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
        { uri, kind });
    return n.empty() ? kind : n;
}

} /* namespace saturn */

#else  /* everywhere with an ordinary filesystem */

namespace saturn {

bool saf_available() { return false; }
void saf_pick() {}
std::vector<SafTree> saf_trees() { return {}; }
void saf_forget(const std::string &) {}
bool saf_ensure_layout(const std::string &) { return false; }
std::vector<SafEntry> saf_list(const std::string &, const std::string &) { return {}; }
std::string saf_stage(const std::string &, const std::string &,
                      const std::string &, const std::string &) { return {}; }
int saf_stage_progress() { return -1; }
std::string saf_folder_name(const std::string &, const std::string &k) { return k; }

} /* namespace saturn */

#endif
