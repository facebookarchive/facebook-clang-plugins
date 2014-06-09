// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/Foundation.h"

@interface myRuleTest: NSObject
{
  dispatch_once_t onceToken_wrong1;
}
@property (atomic, assign) dispatch_once_t onceToken_wrong2;
@end

dispatch_once_t onceToken_correct1;

@implementation myRuleTest
-(void) test {
  dispatch_once_t onceToken_wrong3;
  static dispatch_once_t onceToken_correct2;
}
@end
