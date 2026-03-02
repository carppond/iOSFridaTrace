/*
 * test_stalker_basic.c - Verify Stalker works on macOS before adding trace.
 */

#include "gum.h"
#include <stdio.h>

static int fibonacci(int n)
{
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main(void)
{
    GumStalker *stalker;
    GumStalkerTransformer *transformer;
    GumEventSink *sink;

    printf("[1] Initializing gum...\n");
    gum_init_embedded();

    printf("[2] Creating Stalker...\n");
    stalker = gum_stalker_new();
    gum_stalker_set_trust_threshold(stalker, -1);

    /* No trace recorder - just test vanilla Stalker */
    transformer = gum_stalker_transformer_make_default();
    sink = gum_event_sink_make_default();

    printf("[3] Following self...\n");
    gum_stalker_follow_me(stalker, transformer, sink);

    printf("[4] Running fibonacci(15) = %d\n", fibonacci(15));

    printf("[5] Unfollowing...\n");
    gum_stalker_unfollow_me(stalker);

    printf("[6] Cleanup...\n");
    g_object_unref(transformer);
    g_object_unref(sink);
    g_object_unref(stalker);

    printf("=== Stalker basic test PASSED ===\n");
    return 0;
}
