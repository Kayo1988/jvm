#include "../../jni_internal.h"

// Perform class load-time initializations.
// private static native void init();
static void init()
{
    // todo
    JVM_PANIC("not implement");
}

static JNINativeMethod methods[] = {
    JNINativeMethod_registerNatives,
    { "init", "()V", TA(init) },
};

void java_net_Inet6Address_registerNatives()
{
    registerNatives("java/net/Inet6Address", methods, ARRAY_LENGTH(methods));
}