// Copyright 2013-present Facebook. All Rights Reserved.

#include "FoundationStub.h"

@protocol MyProtocol

@property (nonatomic, copy) NSString *str;

@end

@interface MyClass : NSObject <MyProtocol>

@property (nonatomic, copy) NSString *str;

@property (nonatomic, assign) void *x;
@property (nonatomic, assign) int y;
@property (nonatomic, assign) NSObject *delegate;

- (void)doStuff:(NSString *)s;

@end

@implementation MyClass

- (void)doStuff:(NSString *)s {

  NSLog(@"%s\n", @encode(int **));

  NSLog(@"%d\n", [self respondsToSelector:@selector(doStuff:)]);

  NSLog(@"%d\n", [[self class] conformsToProtocol:@protocol(MyProtocol)]);

  NSUInteger (^block)(NSString *x) = ^(NSString *x) {
    self.str = x;
    return [x length];
  };

  @try {
    NSArray *a = @[@YES];
    NSLog(@"%@\n", a[0]);

    NSDictionary *d = @{ @"key" : @1 };
    NSLog(@"%@\n", d[@"wrong key"]);
    NSLog(@"%p\n", (void *)block);
  }
  @catch (NSException *e) {
    NSLog(@"Exception: %@", e);
  }
  @finally {
    NSLog(@"finally");
  }

  goto theend;
  return;
 theend:
  NSLog(@"jumped");
}

@end

int main(int argc, char** argv) {
  @autoreleasepool {
    [[MyClass new] doStuff:@"hello"];
  }
  return 0;
}
