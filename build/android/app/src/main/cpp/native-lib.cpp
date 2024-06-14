#include <jni.h>
#include <string>
#include <unistd.h>
#include "src/module.h"

extern "C" JNIEXPORT jstring JNICALL
Java_me_dmitrych_dpvm_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject, /* this */
        jstring input) {
    static int called = 0;
    const char *c_input = env->GetStringUTFChars(input, 0);
    const char *c_result;

    if (!called) {
        chdir("/data/user/0/me.dmitrych.dpvm/files");
        called = 1;
    }

    c_result = dpvmGetStr(c_input);

    env->ReleaseStringUTFChars(input, c_input);

    return env->NewStringUTF(c_result);
}