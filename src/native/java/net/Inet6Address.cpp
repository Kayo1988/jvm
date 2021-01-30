#include "../../jni_internal.h"

// Perform class load-time initializations.
// private static native void init();
static void init()
{
    // todo
    jvm_abort("not implement");
}

static JNINativeMethod methods[] = {
    JNINativeMethod_registerNatives,
    { "init", "()V", (void *) init },
};

void java_net_Inet6Address_registerNatives()
{
    registerNatives("java/net/Inet6Address", methods, ARRAY_LENGTH(methods));
}