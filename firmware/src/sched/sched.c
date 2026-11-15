// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
/* Round-robin cooperative scheduler for modd. */

/* Diagnostic stage marker: call at the boundaries of a task's work.  When
 * the code between two marks took SCHED_SLOW_MS or more, the earlier
 * stage is logged.  sched_mark(0) drops the timer (use it before yield(),
 * so other tasks' time is not counted). */
#ifdef CONFIG_DEBUG
#define SCHED_SLOW_MS       300u
#define SCHED_SLOW_LOG_MAX  12u
static uint32_t sched_mark_ms, sched_mark_logs;
static uint8_t  sched_mark_stage;
static void sched_mark(uint8_t stage)
{
    uint32_t now = fm1_ms;
    if (sched_mark_stage && now - sched_mark_ms >= SCHED_SLOW_MS &&
        sched_mark_logs < SCHED_SLOW_LOG_MAX) {
        sched_mark_logs++;
        modd_log_int("ui: slow stage=", (int32_t)sched_mark_stage);
        modd_log_int("ui: slow ms=", (int32_t)(now - sched_mark_ms));
    }
    sched_mark_stage = stage;
    sched_mark_ms = now;
}
#else
#define sched_mark(stage) ((void)0)
#endif

#ifdef CONFIG_SCHED

#define SCHED_CANARY 0xDEADBEEFu

typedef enum {
    SCHED_DEAD = 0,
    SCHED_READY,
    SCHED_SLEEPING,
} sched_state_t;

typedef struct {
    uint32_t *saved_sp;
    uint32_t *stack_base;
    uint32_t  stack_words;
    sched_state_t state;
    uint32_t  wake_ms;
    uint32_t  hwm;
    uint32_t  last_yield_ms;
    uint32_t  max_gap_ms;
    uint32_t  yields;
} sched_task_t;

static struct {
    sched_task_t tasks[CONFIG_SCHED_MAX_TASKS];
    uint32_t     count;
    uint32_t     current;
} sched;

extern void sched_switch(uint32_t **old_sp, uint32_t *new_sp);

/* Diagnostic: the scheduler is cooperative, so a task that keeps the CPU
 * stalls every other one (network polls, USB host, UI).  Log who did it. */
#define SCHED_HOG_MS       300u
#define SCHED_HOG_LOG_MAX  24u
static uint32_t sched_run_since_ms;   /* when the running task got the CPU */
static uint32_t sched_hog_logs;

static void sched_trampoline(void);
static void yield(void);

static void sched_init(void)
{
    uint32_t i;
    for (i = 0; i < CONFIG_SCHED_MAX_TASKS; i++)
        sched.tasks[i].state = SCHED_DEAD;
    sched.count = 0;
    sched.current = 0;
}

static int sched_create(void (*fn)(void), uint32_t *stack, uint32_t size_bytes)
{
    sched_task_t *t;
    uint32_t *sp;
    uint32_t words = size_bytes / 4u;

    if (sched.count >= CONFIG_SCHED_MAX_TASKS)
        return -1;

    t = &sched.tasks[sched.count];
    t->stack_base = stack;
    t->stack_words = words;
    t->state = SCHED_READY;
    t->wake_ms = 0;
    t->hwm = 0;

#ifdef CONFIG_SCHED_STACK_CANARY
    {
        uint32_t i;
        for (i = 0; i < words; i++)
            stack[i] = SCHED_CANARY;
    }
#endif

    sp = stack + words;
    sp = (uint32_t *)((uint32_t)sp & ~7u);

    *(--sp) = (uint32_t)sched_trampoline;
    *(--sp) = 0; /* r15 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r12 */
    *(--sp) = 0; /* r11 */
    *(--sp) = 0; /* r10 */
    *(--sp) = 0; /* r9 */
    *(--sp) = 0; /* r8 */
    *(--sp) = 0; /* r7 */
    *(--sp) = 0; /* r6 */
    *(--sp) = 0; /* r5 */
    *(--sp) = (uint32_t)fn; /* r4 = task entry point */

    t->saved_sp = sp;
    sched.count++;

#ifdef CONFIG_DEBUG
    modd_log_int("sched: task created, id=", (int32_t)(sched.count - 1));
    modd_log_int("  stack=", (int32_t)size_bytes);
#endif

    return 0;
}

static void sched_trampoline(void)
{
    void (*fn)(void);
    __asm__ volatile("%0 = r4" : "=r"(fn));
    fn();
    modd_log("sched: task returned!");
    for (;;)
        yield();
}

static void yield(void)
{
    uint32_t cur = sched.current;
    uint32_t next = cur;
    uint32_t checked = 0;
    sched_task_t *ct = &sched.tasks[cur];
    sched_task_t *nt;

#ifdef CONFIG_DEBUG
    {
        uint32_t held = fm1_ms - sched_run_since_ms;
        if (sched_run_since_ms && held >= SCHED_HOG_MS &&
            sched_hog_logs < SCHED_HOG_LOG_MAX) {
            sched_hog_logs++;
            /* the WiFi task exists only when WiFi is on, so give the count:
             * UI is the last task, services the one before it */
            modd_log_int("sched: hog task=", (int32_t)cur);
            modd_log_int("sched: hog of tasks=", (int32_t)sched.count);
            modd_log_int("sched: hog ms=", (int32_t)held);
        }
    }
#endif
    sched_run_since_ms = fm1_ms ? fm1_ms : 1u;

    do {
        next = (next + 1u) % sched.count;
        nt = &sched.tasks[next];
        if (nt->state == SCHED_SLEEPING) {
            if ((int32_t)(fm1_ms - nt->wake_ms) >= 0)
                nt->state = SCHED_READY;
        }
        if (nt->state == SCHED_READY)
            break;
        checked++;
    } while (checked < sched.count);

    if (next == cur && nt->state != SCHED_READY)
        return;

#ifdef CONFIG_SCHED_STACK_CANARY
    if (nt->stack_base[0] != SCHED_CANARY ||
        nt->stack_base[1] != SCHED_CANARY) {
        modd_log("sched: STACK OVERFLOW!");
        for (;;) ;
    }
    {
        uint32_t i;
        for (i = 2; i < nt->stack_words; i++) {
            if (nt->stack_base[i] != SCHED_CANARY)
                break;
        }
        uint32_t used = (nt->stack_words - i) * 4u;
        if (used > nt->hwm)
            nt->hwm = used;
    }
#endif

    ct->yields++;
    ct->last_yield_ms = fm1_ms;

    if (nt->last_yield_ms) {
        uint32_t gap = fm1_ms - nt->last_yield_ms;
        if (gap > nt->max_gap_ms)
            nt->max_gap_ms = gap;
    }

    sched.current = next;
    sched_switch(&ct->saved_sp, nt->saved_sp);
}

static void sleep_ms(uint32_t ms)
{
    sched_task_t *t = &sched.tasks[sched.current];
    t->wake_ms = fm1_ms + ms;
    t->state = SCHED_SLEEPING;
    yield();
}

static void sched_run(void)
{
    sched_task_t *first;
    if (sched.count == 0)
        return;

    sched.current = 0;
    first = &sched.tasks[0];
    first->state = SCHED_READY;

    modd_log_int("sched: run, tasks=", (int32_t)sched.count);

    {
        uint32_t *dummy_sp;
        sched_switch(&dummy_sp, first->saved_sp);
    }
}

static void sched_log_stats(void)
{
#ifdef CONFIG_DEBUG
    uint32_t i;
    modd_log_int("sched: cur=", (int32_t)sched.current);
    for (i = 0; i < sched.count; i++) {
        sched_task_t *t = &sched.tasks[i];
        modd_log_int("sched: task ", (int32_t)i);
        modd_log_int("  state=", (int32_t)t->state);
        modd_log_int("  yields=", (int32_t)t->yields);
        modd_log_int("  gap=", (int32_t)t->max_gap_ms);
        modd_log_int("  hwm=", (int32_t)t->hwm);
    }
#endif
}

#else /* !CONFIG_SCHED */

static void yield(void) {}
static void sleep_ms(uint32_t ms) { (void)ms; }

#endif /* CONFIG_SCHED */
