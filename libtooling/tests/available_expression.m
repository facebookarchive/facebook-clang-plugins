/*
 * Copyright (c) 2017 - present Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

 @interface NSObject
 @end
 @implementation NSObject
 @end

@interface available_expression : NSObject

@end

@implementation available_expression

- (void)test_no_bug:(int)n and:(available_expression*)data {
  if (@available(macOS 10.13, iOS 11.0, *)) {}

  if (__builtin_available(macos 10.10, ios 8, *)) {}
}

@end
