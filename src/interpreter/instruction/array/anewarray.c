/*
 * Author: Jia Yang
 */

#include "../../../rtda/thread/frame.h"
#include "../../../rtda/heap/arrobj.h"

/*
 * todo
 * 这函数是干嘛的，
 * 实现完全错误
 * 创建一维引用类型数组
 */
void anewarray(struct frame *frame)
{
    struct class *curr_class = frame->m.method->clazz;
    jint arr_len = frame_stack_popi(frame);
    if (arr_len < 0) {
        jthread_throw_negative_array_size_exception(frame->thread, arr_len);
        return;
    }

    // todo arrLen == 0 的情况

    int index = bcr_readu2(&frame->reader);
    const char *class_name = rtcp_get_class_name(frame->m.method->clazz->rtcp, index); // 数组元素的类
    char *arr_class_name = get_arr_class_name(class_name);
    struct class *arr_class = classloader_load_class(curr_class->loader, arr_class_name);
    free(arr_class_name);
    frame_stack_pushr(frame, arrobj_create(arr_class, (size_t) arr_len));
}