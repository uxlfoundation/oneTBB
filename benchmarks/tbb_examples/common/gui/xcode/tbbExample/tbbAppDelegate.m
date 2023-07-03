/*
    Copyright (C) 2005-2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#import "tbbAppDelegate.h"

#if TARGET_OS_IPHONE

@implementation tbbAppDelegate

- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    return YES;
}

- (void)applicationDidEnterBackground:(UIApplication *)application
{
    exit(EXIT_SUCCESS);
}

@end

#elif TARGET_OS_MAC

@implementation tbbAppDelegate

@synthesize window = _window;

//declared in macvideo.cpp file
extern int g_sizex, g_sizey;

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    // Insert code here to initialize your application
    NSRect windowSize;
    windowSize.size.height = g_sizey;
    windowSize.size.width = g_sizex;
    windowSize.origin=_window.frame.origin;
    [_window setFrame:windowSize display:YES];

}

- (BOOL) applicationShouldTerminateAfterLastWindowClosed:(NSApplication *) sender
{
    return YES;
}

@end

#endif
