#include <ctime>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <thread>
#include "cabin.h"
#include "debug.h"
#include "runtime/vm_thread.h"
#include "metadata/class.h"
#include "metadata/method.h"
#include "objects/array.h"
#include "interpreter/interpreter.h"
#include "heap/heap.h"
#include "platform/sysinfo.h"
#include "objects/mh.h"
#include "classpath/classpath.h"

using namespace std;
using namespace std::filesystem;
using namespace utf8;

#if TRACE_JVM
#define TRACE PRINT_TRACE
#else
#define TRACE(...)
#endif

Heap *g_heap;

vector<pair<const utf8_t *, const utf8_t *>> g_properties;

Object *g_sys_thread_group;

vector<Thread *> g_all_threads;

static void showUsage(const char *name);
static void showVersionAndCopyright();

static void *gcLoop(void *arg)
{
    // todo
    return nullptr;
}

static char main_class_name[FILENAME_MAX] = { 0 };
static char *main_func_args[METHOD_PARAMETERS_MAX_COUNT];
static int main_func_args_count = 0;

string g_java_home;

u2 g_classfile_major_version = 0;
u2 g_classfile_manor_version = 0;

Object *g_app_class_loader;
Object *g_platform_class_loader;

static void parseCommandLine(int argc, char *argv[])
{
    // 可执行程序的名字为 argv[0]
    const char *vm_name = argv[0];

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            const char *name = argv[i];
            if (strcmp(name, "-bcp") == 0 or strcmp(name, "-bootclasspath") == 0) { // parse Bootstrap Class Path
                if (++i >= argc) {
                    JVM_PANIC("缺少参数：%s\n", name);
                }
                setBootstrapClasspath(argv[i]);
            } else if (strcmp(name, "-cp") == 0 or strcmp(name, "-classpath") == 0) { // parse Class Path
                if (++i >= argc) {
                    JVM_PANIC("缺少参数：%s\n", name);
                }
                setClasspath(argv[i]);
            } else if (strcmp(name, "-help") == 0 or strcmp(name, "-?") == 0) {
                showUsage(vm_name);
                exit(0);
            } else if (strcmp(name, "-version") == 0) {
                showVersionAndCopyright();
                exit(0);
            } else {
                printf("Unrecognised command line option: %s\n", argv[i]);
                showUsage(vm_name);
                exit(-1);
            }
        } else {
            if (main_class_name[0] == 0) {
                strcpy(main_class_name, argv[i]);
            } else {
                // main function's arguments
                main_func_args[main_func_args_count++] = argv[i];
                if (main_func_args_count > METHOD_PARAMETERS_MAX_COUNT) {
                    // todo many args!!! abort!
                }
            }
        }
    }
}

/*
 * System properties. The following properties are guaranteed to be defined:
 * java.version         Java version number
 * java.vendor          Java vendor specific string
 * java.vendor.url      Java vendor URL
 * java.home            Java installation directory
 * java.class.version   Java class version number
 * java.class.path      Java classpath
 * os.name              Operating System Name
 * os.arch              Operating System Architecture
 * os.version           Operating System Version
 * file.separator       File separator ("/" on Unix)
 * path.separator       Path separator (":" on Unix)
 * line.separator       Line separator ("\n" on Unix)
 * user.name            User account name
 * user.home            User home directory
 * user.dir             User's current working directory
 */
void initProperties()
{
    g_properties.emplace_back("java.version", VM_VERSION);
    g_properties.emplace_back("java.vendor", "cabin" );
    g_properties.emplace_back("java.vendor.url", "doesn't have");
    g_properties.emplace_back("java.home", g_java_home.c_str());
    auto class_version = new utf8_t[32];
    sprintf(class_version, "%d.%d",
            JVM_MUST_SUPPORT_CLASSFILE_MAJOR_VERSION, JVM_MUST_SUPPORT_CLASSFILE_MINOR_VERSION);
    g_properties.emplace_back("java.class.version", class_version);
    g_properties.emplace_back("java.class.path", getClasspath());
    g_properties.emplace_back("os.name", osName());
    g_properties.emplace_back("os.arch", osArch());
    g_properties.emplace_back("os.version",  ""); // todo
    g_properties.emplace_back("file.separator", getFileSeparator());
    g_properties.emplace_back("path.separator", getPathSeparator());
    g_properties.emplace_back("line.separator", getLineSeparator()); // System.out.println最后输出换行符就会用到这个
    g_properties.emplace_back("user.name", "");// todo
    g_properties.emplace_back("user.home", "");// todo
    g_properties.emplace_back("user.dir", "");// todo
    g_properties.emplace_back("user.country", "CN"); // todo
    g_properties.emplace_back("file.encoding", "UTF-8");// todo
    g_properties.emplace_back("sun.stdout.encoding", "UTF-8");// todo
    g_properties.emplace_back("sun.stderr.encoding", "UTF-8");// todo
}

static void initHeap()
{
    g_heap = new Heap;
    if (g_heap == nullptr) {
        JVM_PANIC("init Heap failed"); // todo
    }
}

// JDK major version to classfile major version
#define CLASSFILE_VERSION(jdk_version) (jdk_version -1 + 45)

// Access JAVA_HOME/release file to get the version of JDK
static void readJDKVersion()
{
    ifstream ifs(g_java_home + "/release");
    if(!ifs.is_open()){
        JVM_PANIC("打开文件失败" ); // todo
    }

    string line;
    const char *begin = R"(JAVA_VERSION=")";
    while(getline(ifs, line)) {
        // JAVA_VERSION="x.x.x_xxx" // jdk8及其以下的jdk, JAVA_VERSION="1.8.0_221"
        // JAVA_VERSION="xx.xx.xx"  // jdk9及其以上的jdk, JAVA_VERSION="11.0.1", JAVA_VERSION="15"

        // JDK版本与class file版本对应关系
        // JDK 1.1 = 45，JDK 1.2 = 46, ... 以此类推。
        size_t pos = line.find(begin);
        if (pos != string::npos) {
            pos += strlen(begin);

            size_t underline;
            if ((underline = line.find_first_of('_', pos)) != string::npos) {
                // jdk8及其以下的jdk
                assert(line[pos] == '1');
                assert(line[pos+1] == '.');
                pos += 2; // jump "1."
                g_classfile_major_version = CLASSFILE_VERSION(stoi(line.substr(pos, 1)));
                pos += 2; // jump "x."
                g_classfile_manor_version = stoi(line.substr(pos, underline - pos));
            } else {
                // jdk9及其以上的jdk
                size_t t = line.find_first_of('.', pos);
                if (t == string::npos) {
                    g_classfile_major_version = CLASSFILE_VERSION(stoi(line.substr(pos)));
                    g_classfile_manor_version = 0;
                } else {
                    g_classfile_major_version = CLASSFILE_VERSION(stoi(line.substr(pos, t - pos)));
                    pos = t + 1; // jump '.'
                    t = line.find_first_of('.', pos);
                    if (t == string::npos) {
                        g_classfile_manor_version = stoi(line.substr(pos));
                    } else {
                        g_classfile_manor_version = stoi(line.substr(pos, t - pos));
                    }
                }
            }
            break;
        }
    }
    ifs.close();
    if (g_classfile_major_version > JVM_MUST_SUPPORT_CLASSFILE_MAJOR_VERSION
        || g_classfile_manor_version > JVM_MUST_SUPPORT_CLASSFILE_MINOR_VERSION) {
        JVM_PANIC("不支持的jdk版本"); // todo
    }
}

void initJVM(int argc, char *argv[])
{
    parseCommandLine(argc, argv);

    char *home = getenv("JAVA_HOME");
    if (home == nullptr) {
        JVM_PANIC("java_lang_InternalError, %s\n", "no java lib"); // todo
    }
    g_java_home = home;

//    g_java_home = R"(C:\Program Files\Java\jre1.8.0_221)"; // todo for testing ...........
//    g_java_home = R"(C:\Program Files\Java\jdk-11.0.1)"; // todo for testing .............
//    g_java_home = R"(C:\Program Files\Java\jdk-15)"; // todo for testing .................

    readJDKVersion();

    /* order is important */
    initClasspath();
    initSymbol();
    initHeap();
    initProperties();
    initJNI();
    initClassLoader();
    initMainThread();
    initMethodHandle();

    TRACE("init main thread over\n");
    // 先加载 sun.mis.VM or jdk.internal.misc.VM 类，然后执行其类初始化方法
    Class *vm = nullptr;
    if (IS_GDK9_PLUS) {
        vm = loadBootClass("jdk/internal/misc/VM");
    } else {
        vm = loadBootClass("sun/misc/VM");
    }

    if (vm == nullptr) {
        JVM_PANIC("xxx/misc/VM is null\n");  // todo throw exception
        return;
    }
    // VM类的 "initialize~()V" 方法需调用执行
    // 在VM类的类初始化方法中调用了 "initialize" 方法。
    initClass(vm);

    if (IS_GDK9_PLUS) {
        g_platform_class_loader = getPlatformClassLoader();
        assert(g_platform_class_loader != nullptr);
    }
    g_app_class_loader = getAppClassLoader();
    assert(g_app_class_loader != nullptr);

    // Main Thread Set ContextClassLoader
    g_main_thread->tobj->setRefField(S(contextClassLoader),
                                     S(sig_java_lang_ClassLoader), g_app_class_loader);
}

static void showUsage(const char *name)
{
//    printf("Usage: %s [-options] class [arg1 arg2 ...]\n", name);
//    printf("\nwhere options include:\n");
//    printf("\t-help\t\tprint out this message\n");
//    printf("\t-version\tprint out version number and copyright information\n");
//    printf("\t-verbose\tprint out information about class loading, etc.\n"); // todo verbose
//    printf("\t-verbosegc\tprint out results of garbage collection\n"); // todo verbose gc
//    printf("\t-noasyncgc\tturn off asynchronous garbage collection\n"); // todo noasync gc
//    printf("\t-bcp<path>\tset the bootstrap class path\n");
//    printf("\t-cp<path>\tset the class path\n");

    printf("Usage: %s [-options] class [arg1 arg2 ...]\n", name);
    printf("                 (to run a class file)\n");
    printf("   or  %s [-options] -jar jarfile [arg1 arg2 ...]\n", name); // todo
    printf("                 (to run a standalone jar file)\n");
    printf("\nwhere options include:\n");
    printf("  -cp\t\t   <jar/zip files and directories separated by :>\n");
    printf("  -classpath\t   <jar/zip files and directories separated by :>\n"); // todo
    printf("\t\t   locations where to find application classes\n");
    printf("  -D<name>=<value> set a system property\n");
    printf("  -verbose[:class|gc|jni]\n");// todo
    printf("\t\t   :class print out information about class loading, etc.\n");// todo
    printf("\t\t   :gc print out results of garbage collection\n");
    printf("\t\t   :jni print out native method dynamic resolution\n");
    printf("  -version\t   print out version number and copyright information\n");// todo
    printf("  -? -help\t   print out this message\n");

//    printf("  -Xbootclasspath:%s\n", BCP_MESSAGE);
//    printf("\t\t   locations where to find the system classes\n");
//    printf("  -Xbootclasspath/a:%s\n", BCP_MESSAGE);
//    printf("\t\t   locations are appended to the bootstrap class path\n");
//    printf("  -Xbootclasspath/p:%s\n", BCP_MESSAGE);
//    printf("\t\t   locations are prepended to the bootstrap class path\n");
//    printf("  -Xbootclasspath/c:%s\n", BCP_MESSAGE);
//    printf("\t\t   locations where to find Classpath's classes\n");
//    printf("  -Xbootclasspath/v:%s\n", BCP_MESSAGE);
//    printf("\t\t   locations where to find JamVM's classes\n");
//    printf("  -Xasyncgc\t   turn on asynchronous garbage collection\n");
//    printf("  -Xcompactalways  always compact the heap when garbage-collecting\n");
//    printf("  -Xnocompact\t   turn off heap-compaction\n");
//#ifdef INLINING
//    printf("  -Xnoinlining\t   turn off interpreter inlining\n");
//    printf("  -Xshowreloc\t   show opcode relocatability\n");
//    printf("  -Xreplication:[none|always|<value>]\n");
//    printf("\t\t   none : always re-use super-instructions\n");
//    printf("\t\t   always : never re-use super-instructions\n");
//    printf("\t\t   <value> copy when usage reaches threshold value\n");
//    printf("  -Xcodemem:[unlimited|<size>] (default maximum heapsize/4)\n");
//#endif
//    printf("  -Xms<size>\t   set the initial size of the heap\n");
//    printf("\t\t   (default = MAX(physical memory/64, %dM))\n",
//           DEFAULT_MIN_HEAP/MB);
//    printf("  -Xmx<size>\t   set the maximum size of the heap\n");
//    printf("\t\t   (default = MIN(physical memory/4, %dM))\n",
//           DEFAULT_MAX_HEAP/MB);
//    printf("  -Xss<size>\t   set the Java stack size for each thread "
//                   "(default = %dK)\n", DEFAULT_STACK/KB);
//    printf("\t\t   size may be followed by K,k or M,m (e.g. 2M)\n");
}

static void showVersionAndCopyright()
{
    printf("Java version \"%s\"\n", JAVA_COMPAT_VERSION);
    printf("kayo version %s\n", VM_VERSION);
#if defined(__GNUC__) && defined(__VERSION__)
    printf("Compiled with: g++ %s\n", __VERSION__);
#endif
    printf("Copyright (C) 2020 Yo Ka <kayodesu@outlook.com>\n\n");
//    printf("\nBuild information:\n\nExecution Engine: %s\n", getExecutionEngineName());  // todo
    //   printf("\nBoot Library Path: %s\n", classlibDefaultBootDllPath());  // todo
    //   printf("Boot Class Path: %s\n", classlibDefaultBootClassPath());  // todo
}

/*
 * 测试模块开关。
 * 最多只能有一个开启，需要测试哪个模块开启哪个开关。
 * 全部关闭表示关闭测试，运行JVM。
 */

#define TEST_CONVERT           0
#define TEST_SLOT              0
#define TEST_CLASS_LOADER      0
#define TEST_LOAD_CLASS        0
#define TEST_NEW_ARRAY         0
#define TEST_CLONE_OBJECT      0
#define TEST_PROPERTIES        0
#define TEST_SYSTEM_INFO       0
#define TEST_METHOD_TYPE       0
#define TEST_METHOD_DESCRIPTOR 0
#define TEST_INJECT_FIELD      0

#if !(TEST_CONVERT || TEST_SLOT || TEST_CLASS_LOADER || TEST_LOAD_CLASS || TEST_NEW_ARRAY \
        || TEST_CLONE_OBJECT || TEST_PROPERTIES || TEST_SYSTEM_INFO \
        || TEST_METHOD_TYPE || TEST_METHOD_DESCRIPTOR || TEST_INJECT_FIELD)
int main(int argc, char* argv[])
{
    time_t time1;
    time(&time1);

    initJVM(argc, argv);
    TRACE("init jvm is over.\n");

    if (main_class_name[0] == 0) {  // empty  todo
        JVM_PANIC("no input file\n");
    }

    Class *main_class = loadClass(g_app_class_loader, dot2Slash(main_class_name));
    if (main_class == nullptr) {
        JVM_PANIC("main_class == nullptr"); // todo
    }

    Method *main_method = main_class->lookupStaticMethod(S(main), S(_array_java_lang_String__V));
    if (main_method == nullptr) {
        // java_lang_NoSuchMethodError, "main" todo
        JVM_PANIC("can't find method main."); // todo
    } else {
        if (!main_method->isPublic()) {
            JVM_PANIC("method main must be public."); // todo
        }
        if (!main_method->isStatic()) {
            JVM_PANIC("method main must be static."); // todo
        }
    }

    createVMThread(gcLoop, GC_THREAD_NAME); // gc thread

    // 开始在主线程中执行 main 方法
    TRACE("begin to execute main function.\n");

    // Create the String array holding the command line args
    Array *args = newStringArray(main_func_args_count);
    for (int i = 0; i < main_func_args_count; i++) {
        args->setRef(i, newString(main_func_args[i]));
    }
    // Call the main method
    execJavaFunc(main_method, {args});

    // todo 如果有其他的非后台线程在执行，则main线程需要在此wait

    // todo main_thread 退出，做一些清理工作。

    time_t time2;
    time(&time2);

    printf("run jvm: %lds\n", ((long)(time2)) - ((long)(time1)));
    return 0;
}
#endif

#if (TEST_CONVERT)
static void testConvertDouble(jdouble d)
{
    jlong l = double_to_raw_long_bits(d);
    jdouble d1 = long_bits_to_double(l);
    cout << l << ", ";
    printf("%s, %20.20f\n", (d == d1) ? "true" : "false", d1);
}

static void testDouble2String(jdouble d)
{
    // public static String valueOf(double d);
    Class *c = loadBootClass("java/lang/String");
    Method *m = c->lookupStaticMethod("valueOf", "(D)Ljava/lang/String;");
    slot_t args[2];
    slot::setDouble(args, d);
    jstrref s = slot::getRef(execJavaFunc(m, args));
    cout << s->toUtf8() << endl;
}

static void testFloat2String(jfloat f)
{
    // public static String valueOf(float f);
    Class *c = loadBootClass("java/lang/String");
    Method *m = c->lookupStaticMethod("valueOf", "(F)Ljava/lang/String;");
    jstrref s = slot::getRef(execJavaFunc(m, {slot::fslot(f)}));
    cout << s->toUtf8() << endl;
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    jdouble d = 1.1;
    testConvertDouble(d);
    testDouble2String(d);

    cout << "---" << endl;

    d = 4349790087343.9483948938493;
    testConvertDouble(d);
    testDouble2String(d);

    cout << "---" << endl;

    jfloat f = 1.1f;
    testFloat2String(f);

    cout << "---" << endl;

    f = 123.456f;
    testFloat2String(f);

    return 0;
}
#endif

#if (TEST_SLOT)
union Slot {
public:
    int32_t i;
    float f;
    void *p;
    int64_t j; // jlong
    double d;

    Slot() = default;
    Slot(int i) {this->i = i;}
    void setInt(int v) { i = v; }
    int getInt() const { return i; }
};

int main()
{
    cout << sizeof(short) << endl;
    cout << sizeof(int) << endl;
    cout << sizeof(long) << endl;
    cout << sizeof(float) << endl;
    cout << sizeof(void *) << endl;
    cout << sizeof(int64_t) << endl;
    cout << sizeof(double) << endl;

    cout << sizeof(Slot) << endl;
    Slot slot(8);
    slot.setInt(1000);
    cout << slot.getInt() << endl;


    cout << &slot << endl << &(slot.i) << endl;
}
#endif

#if (TEST_CLASS_LOADER)
static void printAllClassLoaders()
{
    const unordered_set<const Object *> &loaders = getAllClassLoaders();
    for (auto x: loaders) {
        if (x == BOOT_CLASS_LOADER)
            cout << "boot class loader" << endl;
        else
            cout << x->clazz->class_name << endl;
    }
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    printAllClassLoaders();
    cout << "---------------" << endl;

    Object *scl = getAppClassLoader();
    loadClass(scl, "HelloWorld");

    printAllClassLoaders();
}
#endif

#if (TEST_LOAD_CLASS)
static void printClass(Class *c)
{
    cout << "---------------" << endl;
    cout << c->class_name << ", ";
    cout << c->java_mirror->jvm_mirror->class_name << ", ";
    cout << (c == c->java_mirror->jvm_mirror) << endl;
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

//    printBootLoadedClasses();
    printClass(loadBootClass("boolean"));
    printClass(loadClass(getSystemClassLoader(), "HelloWorld"));
}
#endif

#if (TEST_NEW_ARRAY)
void initIntArray(Array *arr, jint dim, const jint *lens)
{
    if (dim <= 0)
        return;

    jint arr_len = lens[0];

    if (dim == 1) {
        for (jint i = 0; i < arr_len; i++)
            arr->setInt(i, i+1);
        return;
    }

    for (jint i = 0; i < arr_len; i++) {
        initIntArray(arr->get<Array *>(i), dim - 1, lens + 1);
    }
}

void printIntArray(Array *arr, jint dim, const jint *lens)
{
    if (dim <= 0)
        return;

    jint arr_len = lens[0];

    if (dim == 1) {
        for (jint i = 0; i < arr_len; i++) {
            printf("%d, ", arr->get<jint>(i));
        }
        printf("\n");
        return;
    }

    for (jint i = 0; i < arr_len; i++) {
        printIntArray(arr->get<Array *>(i), dim - 1, lens + 1);
    }
}

void test1()
{
    printf("test1\n");
    static const int ARR_LEN = 8;

    Array *arr = newArray("[I", ARR_LEN);
    initIntArray(arr, 1, &ARR_LEN);
    printIntArray(arr, 1, &ARR_LEN);
}

void test2()
{
    printf("test2\n");

    Class *ac = loadArrayClass("[[I");
    jint dim = 2;
    jint lens[] = { 3, 5 };
    Array *arr = ac->allocMultiArray(dim, lens);

    initIntArray(arr, dim, lens);
    printIntArray(arr, dim, lens);
}

void test3()
{
    printf("test3\n");

    Class *ac = loadArrayClass("[[[I");
    jint dim = 3;
    jint lens[] = { 2, 3, 4 };
    Array *arr = ac->allocMultiArray(dim, lens);

    initIntArray(arr, dim, lens);
    printIntArray(arr, dim, lens);
    printf("\n");
}

void test4()
{
    printf("test4\n");

    Class *ac = loadArrayClass("[[[[I");
    jint dim = 4;
    jint lens[] = { 2, 3, 4, 20 };
    Array *arr = ac->allocMultiArray(dim, lens);

    initIntArray(arr, dim, lens);
    printIntArray(arr, dim, lens);
    printf("\n");
}

void test7()
{
    printf("test4\n");

    Class *ac = loadArrayClass("[[[[[[[I");
    jint dim = 7;
    jint lens[] = { 2, 3, 4, 5, 6, 7, 8 };

    Array *arr = ac->allocMultiArray(dim, lens);
    initIntArray(arr, dim, lens);
    printIntArray(arr, dim, lens);
    printf("\n");
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    test1();
    test2();
    test3();
    test4();
    test7();
}
#endif

#if (TEST_CLONE_OBJECT)
#define LEN 20

static void testCloneIntArray()
{
    cout << "-------------------" << endl;

    Array *x = newArray(loadArrayClass("[I"), LEN);
    for (int i = 0; i < LEN; ++i) {
        x->setInt(i, i);
    }

    auto x0 = (Array *) x->clone();
    for (int i = 0; i < LEN; ++i) {
        x0->setInt(i, i + 100);
    }

    cout << x << ", " << x->len << endl;
    for (int i = 0; i < LEN; ++i) {
        cout << x->get<jint>(i);
        if (i == LEN - 1)
            cout << "." << endl;
        else
            cout << ", ";
    }

    cout << x0 << ", " << x0->len << endl;
    for (int i = 0; i < LEN; ++i) {
        cout << x0->get<jint>(i);
        if (i == LEN - 1)
            cout << "." << endl;
        else
            cout << ", ";
    }
}

static void testCloneStringArray()
{
    cout << "-------------------" << endl;

    Array *x = newArray(loadArrayClass("[Ljava/lang/String;"), LEN);
    for (int i = 0; i < LEN; ++i) {
        ostringstream oss;
        oss << i;
        x->setRef(i, newString(oss.str().c_str()));
    }

    auto x0 = (Array *) x->clone();
    for (int i = 0; i < LEN; ++i) {
        ostringstream oss;
        oss << i + 100;
        x0->setRef(i, newString(oss.str().c_str()));
    }

    cout << x << ", " << x->len << endl;
    for (int i = 0; i < LEN; ++i) {
        cout << x->get<Object *>(i)->toUtf8();
        if (i == LEN - 1)
            cout << "." << endl;
        else
            cout << ", ";
    }

    cout << x0 << ", " << x0->len << endl;
    for (int i = 0; i < LEN; ++i) {
        cout << x0->get<Object *>(i)->toUtf8();
        if (i == LEN - 1)
            cout << "." << endl;
        else
            cout << ", ";
    }
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    testCloneIntArray();
    testCloneStringArray();

    return 0;
}
#endif

#if (TEST_PROPERTIES)
int main()
{
    initProperties();

    for (auto &x: g_properties) {
        cout << x.first << ": " << x.second << endl;
    }
}
#endif

#if (TEST_SYSTEM_INFO)
int main()
{
    cout << "processor number: " << processorNumber() << endl;
    cout << "page size: " << pageSize() << endl;
    cout << "os name: " << osName() << endl;
    cout << "os arch: " << osArch() << endl;
    cout << "time zone: " << getTimeZone() << endl;
}
#endif

#if (TEST_METHOD_DESCRIPTOR || TEST_METHOD_TYPE)
// Some method descriptors for testing.
static const char *method_descriptors[] = {
        "()V",
        "(I)V",
        "(B)C",
        "(Ljava/lang/Integer;)V",
        "(Ljava/lang/Object;[[BLjava/lang/Integer;[Ljava/lang/Object;)V",
        "(II[Ljava/lang/String;)Ljava/lang/Integer;",
        "([Ljava/io/File;)Ljava/lang/Object;",
        "([[[Ljava/lang/Double;)[[Ljava/lang/Object;",
        "(ZBSIJFD)[[Ljava/lang/String;",
        "(ZZZZZZZZZZZZZZZZ)Z",
};
#endif

#if (TEST_METHOD_DESCRIPTOR)
#include "metadata/descriptor.h"

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    jref scl = getSystemClassLoader();

    for (auto &d : method_descriptors) {
        cout << d << endl;
        cout << numElementsInDescriptor(d + 1, strchr(d, ')')) << " | ";
        pair<Array *, ClassObject *> p = parseMethodDescriptor((char *) d, scl);
        assert(p.first != nullptr);
        assert(p.second != nullptr);
        for (int i = 0; i < p.first->len; i++) {
            auto co = p.first->get<ClassObject *>(i);
            cout << co->jvm_mirror->class_name;
            if (i < p.first->len - 1)
                cout << ", ";
            else
                cout << " | ";
        }
        cout << p.second->jvm_mirror->class_name << endl;

        cout << "--- unparse ---" << unparseMethodDescriptor(p.first, p.second) << endl << endl;
    }

    cout << "---------" << endl;
    cout << unparseMethodDescriptor(nullptr, nullptr) << endl;
}
#endif

#if (TEST_METHOD_TYPE)
#include "metadata/descriptor.h"

static void printMT(jref mt)
{
    auto ptypes = mt->getRefField<Array>("ptypes", S(array_java_lang_Class)); // Class<?>[]
    for (int i = 0; i < ptypes->len; ++i) {
        auto t = ptypes->get<ClassObject *>(i);
        cout << t->jvm_mirror->class_name;
        if (i < ptypes->len - 1)
            cout << ", ";
        else
            cout << endl;
    }
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    for (auto &d : method_descriptors) {
        cout << "--------------- " << endl << d << endl;
        jref mt = findMethodType(d, g_app_class_loader);
        printMT(mt);

        string desc = unparseMethodDescriptor(mt);
        cout << desc.c_str() << endl;

        if (strcmp(d, desc.c_str()) != 0)
            throw runtime_error("error");
    }
}
#endif

#if (TEST_INJECT_FIELD)
static void inject(const char *class_name)
{
    cout << "--------- begin ----------" << endl;

    Class *c = loadBootClass(class_name);
    cout << c->toString().c_str() << endl;

    // 因为 injectInstField 只能在loaded之后进行，
    // 所以这里为了测试强制设置一下。
    auto state = c->state;
    c->state = Class::LOADED;
    c->injectInstField("inject1", "C");
    c->injectInstField("inject2", "J");
    c->injectInstField("inject3", "I");
    c->state = state;
    cout << c->toString().c_str();

    cout << "---------  end  ----------" << endl;
}

int main(int argc, char *argv[])
{
    initJVM(argc, argv);

    try {
        inject("java/lang/Object");
        inject("java/lang/Class");

        // 第二次注入 java/lang/Object
        inject("java/lang/Object");
    } catch (runtime_error &e) {
        cout << e.what();
    }

    return 0;
}
#endif