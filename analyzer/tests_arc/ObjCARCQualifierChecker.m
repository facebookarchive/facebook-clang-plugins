// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/Foundation.h"

@interface TestARCQualifiers : NSObject
@end

@implementation TestARCQualifiers

-(void)exec:(void(^)(void))block
{
  block();
}

-(void)doStuff
{
  NSString *testString = @"test";
  NSString *argString = @"test";
  SEL selector = @selector(stringByReplacingCharactersInRange:withString:);
  NSMethodSignature *signature = [NSString instanceMethodSignatureForSelector:selector];
  NSInvocation *invocation = [NSInvocation invocationWithMethodSignature:signature];
  [invocation setTarget:testString];
  [invocation setSelector:selector];
  NSRange range;
  [invocation setArgument:&range atIndex:2];

  __unsafe_unretained NSString *goodString = nil;
  NSString *badString = nil;

  [invocation getArgument:&goodString atIndex:3];
  [invocation getArgument:&badString atIndex:3];

  [invocation getArgument:&range atIndex:2];

  [self exec:^{
    NSString *badString = nil;
    [invocation getArgument:&badString atIndex:3];
  }];

  [invocation invoke];

  [invocation getReturnValue:&goodString];
  [invocation getReturnValue:&badString];
}

@end
