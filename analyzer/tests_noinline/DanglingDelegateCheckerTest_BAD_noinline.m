// Copyright 2004-present Facebook. All Rights Reserved.

#import <Foundation/Foundation.h>

@interface Test : NSObject
@end

@interface Worker : NSObject

@property (atomic, assign) Test *delegate;

@end

@interface Child : Test {
  Worker *_worker1;
}

@property (atomic, retain) Worker *worker2;

@property (atomic, retain) Worker *worker3;

@end

@implementation Child

- (void)setDelegates
{
  // This marks _worker1.delegate _worker2.delegate _worker3.delegate as "dangerous"
  _worker1.delegate = self;
  [self.worker2 setDelegate:self];
  [_worker3 setDelegate:self];
}

- (void)bad1
{
  [_worker1 release];
}

- (void)bad2
{
  [_worker2 autorelease];
}

- (void)bad3
{
  self.worker2 = nil;
}

- (void)bad4
{
  [_worker2 setDelegate:nil];
  [_worker2 setDelegate:self];
  // previous line did reactivate the checks
  self.worker2 = nil;
}

// bad too despite the init method
- (id)init
{
  if (self = [super init]) {
    [self.worker2 setDelegate:self];
    // previous line did reactivate the checks
    self.worker2 = nil;
  }
  return self;
}

// NOT SUPPORTED: the rule currently does not know that the autorelease cannot trigger dealloc here
- (void)unsupported1
{
   [_worker1 retain];
   [_worker1 release];
}

@end
