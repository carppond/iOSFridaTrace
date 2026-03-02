/*
 * FridaTrace Demo - AppDelegate
 *
 * Demonstrates how to start/stop instruction tracing in an iOS app.
 */

#import "AppDelegate.h"
#import "fridatrace.h"

@interface AppDelegate ()

@property (nonatomic, assign) GumTraceSession *traceSession;

@end

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    /*
     * === Start instruction tracing ===
     *
     * Parameters:
     *   module_name: Name of the executable to trace.
     *                Use NULL to trace all modules (global).
     *                Use the app's binary name to trace only your code.
     *
     *   start_addr:  Start address within the module (0 = from module base).
     *
     *   size:        Size of address range to trace (0 = entire module).
     *
     *   output_path: File path for the trace binary output.
     *                On iOS, use the app's Documents directory.
     */
    NSString *docsDir = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *tracePath = [docsDir stringByAppendingPathComponent:@"trace.bin"];

    NSLog(@"[FridaTrace] Starting trace, output: %@", tracePath);

    /* Trace only this app's main executable */
    NSString *execName = [[NSBundle mainBundle] objectForInfoDictionaryKey:
        @"CFBundleExecutable"];

    self.traceSession = gum_trace_start(
        [execName UTF8String],  /* module name */
        0,                      /* start address (0 = module base) */
        0,                      /* size (0 = entire module) */
        [tracePath UTF8String]  /* output file path */
    );

    NSLog(@"[FridaTrace] Tracing started for module: %@", execName);

    return YES;
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    if (self.traceSession != NULL)
    {
        guint64 count = gum_trace_get_record_count(self.traceSession);
        guint64 dropped = gum_trace_get_dropped_count(self.traceSession);

        NSLog(@"[FridaTrace] Stopping trace. Records: %llu, Dropped: %llu",
              count, dropped);

        gum_trace_stop(self.traceSession);
        self.traceSession = NULL;

        NSLog(@"[FridaTrace] Trace saved to Documents/trace.bin");
    }
}

@end
