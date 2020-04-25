/*
 * Author: Yo Ka
 */

#include "../../../symbol.h"
#include "../../jnidef.h"
#include "../../../util/convert.h"

// public native int hashCode();
static jint hashCode(JNIEnv *env, jobject _this)
{
    return (jint)(intptr_t)_this;
}

// protected native Object clone() throws CloneNotSupportedException;
static jobject clone(JNIEnv *env, jobject _this)
{
    if (is_subclass_of(obj_get_class(_this), (*env)->FindClass(env, S(java_lang_Cloneable))) == 0) {
        jclass exception = (*env)->FindClass(env, S(java_lang_CloneNotSupportedException));
        (*env)->ThrowNew(env, exception, NULL);
    }
    return obj_clone(_this);
}

// public final native Class<?> getClass();
static jobject getClass(JNIEnv *env, jobject _this)
{
    return obj_get_class(_this); // todo 对不对
}

// public final native void notifyAll();
static void notifyAll(JNIEnv *env, jobject _this)
{
    // todo
}

// public final native void notify();
static void notify(JNIEnv *env, jobject _this)
{
    // todo
}

// public final native void wait(long timeout) throws InterruptedException;
static void wait(JNIEnv *env, jobject _this, jlong timeout)
{
    // todo
}

static JNINativeMethod methods[] = {
        JNINativeMethod_registerNatives,
        { "hashCode", "()I", (void *) hashCode },
        { "getClass", "()Ljava/lang/Class;", (void *) getClass },
        { "clone", "()Ljava/lang/Object;", (void *) clone },
        { "notifyAll", "()V", (void *) notifyAll },
        { "notify", "()V", (void *) notify },
        { "wait", "(J)V", (void *) wait },
};

void java_lang_Object_registerNatives()
{
    registerNatives0("java/lang/Object", methods, ARRAY_LENGTH(methods));
}
