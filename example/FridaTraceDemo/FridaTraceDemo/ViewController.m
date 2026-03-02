/*
 * FridaTrace Demo - ViewController
 *
 * Simple UI to control tracing and run test workloads.
 */

#import "ViewController.h"
#import "fridatrace.h"
#import <dlfcn.h>

@interface ViewController ()

@property (nonatomic, assign) GumTraceSession *session;
@property (nonatomic, assign) BOOL tracing;
@property (nonatomic, strong) NSString *tracePath;

@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *countLabel;
@property (nonatomic, strong) UIButton *traceButton;
@property (nonatomic, strong) UIButton *workloadButton;
@property (nonatomic, strong) UITextView *logView;

@end

@implementation ViewController

#pragma mark - Lifecycle

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor systemBackgroundColor];
    [self setupUI];

    NSString *docsDir = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    self.tracePath = [docsDir stringByAppendingPathComponent:@"trace.bin"];
}

#pragma mark - UI

- (void)setupUI
{
    /* Title */
    UILabel *title = [[UILabel alloc] init];
    title.text = @"FridaTrace Demo";
    title.font = [UIFont boldSystemFontOfSize:24];
    title.textAlignment = NSTextAlignmentCenter;

    /* Status */
    self.statusLabel = [[UILabel alloc] init];
    self.statusLabel.text = @"Status: Idle";
    self.statusLabel.font = [UIFont systemFontOfSize:16];
    self.statusLabel.textColor = [UIColor secondaryLabelColor];
    self.statusLabel.textAlignment = NSTextAlignmentCenter;

    /* Record count */
    self.countLabel = [[UILabel alloc] init];
    self.countLabel.text = @"Records: 0 | Dropped: 0";
    self.countLabel.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    self.countLabel.textAlignment = NSTextAlignmentCenter;

    /* Trace button */
    self.traceButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.traceButton setTitle:@"Start Tracing" forState:UIControlStateNormal];
    self.traceButton.titleLabel.font = [UIFont boldSystemFontOfSize:18];
    self.traceButton.backgroundColor = [UIColor systemGreenColor];
    [self.traceButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.traceButton.layer.cornerRadius = 12;
    [self.traceButton addTarget:self action:@selector(toggleTrace)
               forControlEvents:UIControlEventTouchUpInside];

    /* Workload button */
    self.workloadButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.workloadButton setTitle:@"Run Workload" forState:UIControlStateNormal];
    self.workloadButton.titleLabel.font = [UIFont systemFontOfSize:16];
    self.workloadButton.backgroundColor = [UIColor systemBlueColor];
    [self.workloadButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    self.workloadButton.layer.cornerRadius = 12;
    self.workloadButton.enabled = NO;
    self.workloadButton.alpha = 0.5;
    [self.workloadButton addTarget:self action:@selector(runWorkload)
                  forControlEvents:UIControlEventTouchUpInside];

    /* Log view */
    self.logView = [[UITextView alloc] init];
    self.logView.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    self.logView.editable = NO;
    self.logView.backgroundColor = [UIColor secondarySystemBackgroundColor];
    self.logView.layer.cornerRadius = 8;
    self.logView.text = @"Ready.\n";

    /* Stack view */
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[
        title, self.statusLabel, self.countLabel,
        self.traceButton, self.workloadButton, self.logView
    ]];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 12;
    stack.translatesAutoresizingMaskIntoConstraints = NO;

    [self.view addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:20],
        [stack.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
        [stack.bottomAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.bottomAnchor constant:-20],
        [self.traceButton.heightAnchor constraintEqualToConstant:50],
        [self.workloadButton.heightAnchor constraintEqualToConstant:44],
    ]];
}

#pragma mark - Actions

- (void)toggleTrace
{
    if (!self.tracing)
        [self startTrace];
    else
        [self stopTrace];
}

- (void)startTrace
{
    NSString *execName = [[NSBundle mainBundle] objectForInfoDictionaryKey:
        @"CFBundleExecutable"];

    [self log:@"Starting trace for module: %@", execName];

    self.session = gum_trace_start(
        [execName UTF8String], 0, 0,
        [self.tracePath UTF8String]);

    if (self.session == NULL)
    {
        [self log:@"ERROR: Failed to start trace!"];
        return;
    }

    self.tracing = YES;
    self.statusLabel.text = @"Status: TRACING";
    self.statusLabel.textColor = [UIColor systemRedColor];
    [self.traceButton setTitle:@"Stop Tracing" forState:UIControlStateNormal];
    self.traceButton.backgroundColor = [UIColor systemRedColor];
    self.workloadButton.enabled = YES;
    self.workloadButton.alpha = 1.0;

    [self log:@"Trace started. Output: %@", self.tracePath];

    /* Periodically update record count */
    [self scheduleCountUpdate];
}

- (void)stopTrace
{
    if (!self.session) return;

    guint64 total   = gum_trace_get_record_count(self.session);
    guint64 dropped = gum_trace_get_dropped_count(self.session);

    gum_trace_stop(self.session);
    self.session = NULL;
    self.tracing = NO;

    self.statusLabel.text = @"Status: Stopped";
    self.statusLabel.textColor = [UIColor systemGreenColor];
    [self.traceButton setTitle:@"Start Tracing" forState:UIControlStateNormal];
    self.traceButton.backgroundColor = [UIColor systemGreenColor];
    self.workloadButton.enabled = NO;
    self.workloadButton.alpha = 0.5;

    self.countLabel.text = [NSString stringWithFormat:
        @"Records: %llu | Dropped: %llu", total, dropped];

    [self log:@"Trace stopped. Records: %llu, Dropped: %llu", total, dropped];
    [self log:@"Trace file: %@", self.tracePath];

    /* Check file size */
    NSDictionary *attrs = [[NSFileManager defaultManager]
        attributesOfItemAtPath:self.tracePath error:nil];
    if (attrs)
    {
        unsigned long long size = [attrs fileSize];
        [self log:@"File size: %.2f MB", size / (1024.0 * 1024.0)];
    }
}

- (void)runWorkload
{
    [self log:@"Running workloads..."];

    /* Fibonacci */
    int fib = [self fibonacci:20];
    [self log:@"  fibonacci(20) = %d", fib];

    /* String operations */
    char buf[256];
    for (int i = 0; i < 10; i++)
    {
        snprintf(buf, sizeof(buf), "iteration %d: FridaTrace test", i);
        strlen(buf);
    }
    [self log:@"  String work done."];

    /* ObjC calls */
    NSString *s = [NSString stringWithFormat:@"ObjC test %d", 42];
    NSArray *arr = @[@1, @2, @3];
    NSUInteger sum = 0;
    for (NSNumber *n in arr) sum += [n unsignedIntegerValue];
    [self log:@"  ObjC work done. s=%@, sum=%lu", s, (unsigned long)sum];

    /* Math */
    volatile double result = 1.0;
    for (int i = 1; i <= 100; i++) result *= 1.01;
    [self log:@"  Math work done."];

    /* Update count */
    if (self.session)
    {
        guint64 total   = gum_trace_get_record_count(self.session);
        guint64 dropped = gum_trace_get_dropped_count(self.session);
        self.countLabel.text = [NSString stringWithFormat:
            @"Records: %llu | Dropped: %llu", total, dropped];
    }

    [self log:@"Workloads complete."];
}

#pragma mark - Helpers

- (int)fibonacci:(int)n
{
    if (n <= 1) return n;
    return [self fibonacci:n - 1] + [self fibonacci:n - 2];
}

- (void)scheduleCountUpdate
{
    if (!self.tracing) return;

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC),
        dispatch_get_main_queue(), ^{
            if (self.session && self.tracing)
            {
                guint64 total   = gum_trace_get_record_count(self.session);
                guint64 dropped = gum_trace_get_dropped_count(self.session);
                self.countLabel.text = [NSString stringWithFormat:
                    @"Records: %llu | Dropped: %llu", total, dropped];
                [self scheduleCountUpdate];
            }
        });
}

- (void)log:(NSString *)fmt, ...
{
    va_list args;
    va_start(args, fmt);
    NSString *msg = [[NSString alloc] initWithFormat:fmt arguments:args];
    va_end(args);

    NSLog(@"[FridaTrace] %@", msg);

    dispatch_async(dispatch_get_main_queue(), ^{
        self.logView.text = [self.logView.text stringByAppendingFormat:
            @"%@\n", msg];
        /* Auto-scroll to bottom */
        NSRange bottom = NSMakeRange(self.logView.text.length - 1, 1);
        [self.logView scrollRangeToVisible:bottom];
    });
}

@end
