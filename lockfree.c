/* Improvment to the implementation on 
 * http://www.linuxjournal.com/content/lock-free-multi-producer-multi-consumer-queue-ring-buffer .
 * There's a subtle bug in that code, that's why I write my own code here.
 * Althrough most part of the implementation is his:)
 * To be focused, I only implement one write-thread.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sched.h>

#define N 8 // eight read threads here.
#define MAX_SIZE 4096 // allocate memory size 4096 * sizeof(long).
#define Q_SIZE   2048
#define Q_MASK   (2048 - 1)
#define MAX_VALUE      0xffffffffUL

#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)
#define barrier()    asm volatile("":::"memory")

struct ring {
    u_int32_t size;        // real size of this ring. should be less than MAX_SIZE.
    u_int32_t write_ptr;   // record the wirte position of the RX
    u_int32_t read_ptr;    // global read shared by all worker.
    u_int32_t thr_read[N]; // record the read position of each worker
    void *pkt[MAX_SIZE];
};

typedef struct ring ring_t;
typedef struct mbuf mbuf_t;

ring_t ring_buf; // a shared variable by all threads

void ring_init()
{
    char i;

    ring_buf.size = 2048;
    ring_buf.write_ptr = 0;
    ring_buf.read_ptr = 0;
    for (i = 0; i < N; i++) {
        ring_buf.thr_read[i] = MAX_VALUE;
    }
}

mbuf_t *read(u_int32_t hwt_id)
{
    mbuf_t *ret;
    ring_buf.thr_read[hwt_id] = ring_buf.read_ptr;
    ring_buf.thr_read[hwt_id] = __sync_fetch_and_add(&ring_buf.read_ptr, 1);

    while (unlikely(ring_buf.thr_read[hwt_id] >= ring_buf.write_ptr)) {
        sched_yield();
    }

    ret = ring_buf.pkt[ring_buf.thr_read[hwt_id] & Q_MASK];
    /* We have to insert barrier() here, that's the bug in the 
     * original implementation. 
     * Now I explain in detail why we should insert memory fence here.
     * 
     * If there's no barrier here, these code could be optimized by
     * the compiler as bellow,
     * {
     *     register u_int32_t tmp = ring_buf.thr_read[hwt_id] & Q_MASK;
     *     ring_buf.thr_read[hwt_id] = MAX_VALUE;
     *     ret = ring_buf.pkt[tmp]; <<<<< Instruction A
     * }
     * As the value of ring_buf.thr_read[hwt_id] is updated, then the
     * previous value of it could be used by other threads. As a result
     * of it, the Instruction A could be executed simulatently with the
     * write thread. Then error occurs.
     *
     * The reason the compiler optimizes it as this is that,
     * the compiler will group the operations on same memory together
     * to improve cache-hit. 
     */
    barrier();
    ring_buf.thr_read[hwt_id] = MAX_VALUE;

    return ret;
}

void write(mbuf_t *ptr)
{
    register u_int32_t last_read;
    register u_int32_t tmp;
    char i;

    last_read = ring_buf.read_ptr;
    for (i = 0; i < N; i++) {
        tmp = ring_buf.thr_read[i];
        barrier();
        if (last_read > tmp) {
            last_read = tmp;
        }
    }
    while (unlikely(ring_buf.write_ptr >= last_read + Q_SIZE)) {
        sched_yield();
        for (i = 0; i < N; i++) {
            tmp = ring_buf.thr_read[i];
            barrier();
            if (last_read > tmp) {
                last_read = tmp;
            }
        }
    }

    /* if the size is not the power of 2, we could use '%' instead.
     * we all know that '&' is a little faster than '%'.
     */
    ring_buf.pkt[ring_buf.write_ptr % Q_SIZE] = ptr;
    /* because ring_buf.write_ptr is also read by read-thread
     * So we must use atomic_add here.
     */
    __sync_fetch_and_add(&ring_buf.write_ptr, 1);
}

int main(void)
{
    ring_init();

    /*TODO*/

    return 0;
}

