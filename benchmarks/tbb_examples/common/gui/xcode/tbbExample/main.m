/*
    Copyright (C) 2005-2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#import <Availability.h>
#import <Foundation/Foundation.h>

#if TARGET_OS_IPHONE

#import <UIKit/UIKit.h>
#import "tbbAppDelegate.h"

void get_screen_resolution(int *x, int *y) {
    // Getting landscape screen resolution in any case
    CGRect imageRect = [[UIScreen mainScreen] bounds];
    *x=imageRect.size.width>imageRect.size.height?imageRect.size.width:imageRect.size.height;
    *y=imageRect.size.width<imageRect.size.height?imageRect.size.width:imageRect.size.height;
    return;
}

int cocoa_main(int argc, char * argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([tbbAppDelegate class]));
    }
}

#elif TARGET_OS_MAC

#import <Cocoa/Cocoa.h>

int cocoa_main(int argc, char *argv[])
{
    return NSApplicationMain(argc, (const char **)argv);
}
#endif
