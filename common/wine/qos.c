/* qos.dylib: give Wine's threads the macOS QoS class "user interactive".
 *
 * Why: the athei CrossOver 26.3 build makes its threads with the default QoS
 * class. macOS then lets a timed sleep end several milliseconds late (timer
 * coalescing). Commandos limits its frame rate with Sleep(), so each frame
 * took about 7 ms too long (17.8 instead of 19.5 frames per second). The
 * Wine 8 build (Homebrew wine-crossover 23.7.1) sets a QoS class on each
 * thread with pthread_attr_set_qos_class_np, so it did not have the problem.
 * Measured 2026-10-07: the game's frame-limit Sleep() ended 6 to 8 ms late
 * without this file and about 1.4 ms late with it. "user initiated" was not
 * enough (no change).
 *
 * How: the launcher sets DYLD_INSERT_LIBRARIES to this file. It replaces
 * pthread_create (dyld interposing) and sets the class on the new thread's
 * attributes, and it sets the class of the main thread at load. It must be
 * a universal file (x86_64 for Wine, arm64 for x87sidecar, which gets the
 * same environment). Build: common/wine/install-athei.sh.
 */
#include <pthread.h>
#include <pthread/qos.h>

static int qos_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                              void *(*start)(void *), void *arg)
{
    pthread_attr_t local;
    int ret;

    if (attr)
    {
        local = *attr;
    }
    else
    {
        pthread_attr_init(&local);
    }
    pthread_attr_set_qos_class_np(&local, QOS_CLASS_USER_INTERACTIVE, 0);
    ret = pthread_create(thread, &local, start, arg);
    if (!attr) pthread_attr_destroy(&local);
    return ret;
}

__attribute__((used)) static const struct { const void *replacement, *original; } interposers[]
    __attribute__((section("__DATA,__interpose"))) = {
    { (const void *)qos_pthread_create, (const void *)pthread_create },
};

__attribute__((constructor)) static void qos_init(void)
{
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}
