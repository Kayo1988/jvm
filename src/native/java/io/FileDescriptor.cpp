#include "../../jni_internal.h"
#include "../../../runtime/vm_thread.h"

/*
 * This routine initializes JNI field offsets for the class
 *
 * private static native void initIDs();
 */
static void initIDs()
{
    // todo
//    JVM_PANIC("error\n");
}

/*
 * private native long set(int d);
 */
static jlong set(jobject _this, jint d)
{
    // todo
    return 123;
}

/**
 * Force all system buffers to synchronize with the underlying
 * device.  This method returns after all modified data and
 * attributes of this FileDescriptor have been written to the
 * relevant device(s).  In particular, if this FileDescriptor
 * refers to a physical storage medium, such as a file in a file
 * system, sync will not return until all in-memory modified copies
 * of buffers associated with this FileDesecriptor have been
 * written to the physical medium.
 *
 * sync is meant to be used by code that requires physical
 * storage (such as a file) to be in a known state  For
 * example, a class that provided a simple transaction facility
 * might use sync to ensure that all changes to a file caused
 * by a given transaction were recorded on a storage medium.
 *
 * sync only affects buffers downstream of this FileDescriptor.  If
 * any in-memory buffering is being done by the application (for
 * example, by a BufferedOutputStream object), those buffers must
 * be flushed into the FileDescriptor (for example, by invoking
 * OutputStream.flush) before that data will be affected by sync.
 *
 * @exception SyncFailedException
 *        Thrown when the buffers cannot be flushed,
 *        or because the system cannot guarantee that all the
 *        buffers have been synchronized with physical media.
 * @since     JDK1.1
 *
 * public native void sync() throws SyncFailedException;
 */
static void sync(jobject _this)
{
    JVM_PANIC("error\n"); // todo
}

/**
 * On Windows return the handle for the standard streams.
 *
 * private static native long getHandle(int d);
 */
static jlong getHandle(jint d)
{
    // todo
    return 123;
    JVM_PANIC("error\n"); // todo
}

/**
 * Returns true, if the file was opened for appending.
 *
 * private static native boolean getAppend(int fd);
 */
static jbool getAppend(jint fd)
{
    // todo
    return jfalse;
    JVM_PANIC("error\n"); // todo
}

/*
 * Close the raw file descriptor or handle, if it has not already been closed
 * and set the fd and handle to -1.
 *
 * private native void close0() throws IOException;
 */
static void close0()
{
    // todo ..
}

static JNINativeMethod methods[] = {
        JNINativeMethod_registerNatives,
        { "initIDs", "()V", TA(initIDs) },
        { "set", "(I)J", TA(set) },
        { "sync", "()V", TA(sync) },
        { "getHandle", "(I)J", TA(getHandle) },
        { "getAppend", "(I)Z", TA(getAppend) },
        { "close0", "()V", TA(close0) },
};

void java_io_FileDescriptor_registerNatives()
{
    registerNatives("java/io/FileDescriptor", methods, ARRAY_LENGTH(methods));
}