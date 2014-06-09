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

@property (atomic, strong) Worker *worker2;

@property (atomic, strong) Worker *worker3;

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
  _worker1 = nil;
}

- (void)bad2
{
  _worker3 = nil;
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
  _worker2 = nil;
}

// bad too despite the init method
- (id)init
{
  if (self = [super init]) {
    [self.worker2 setDelegate:self];
    // previous line did reactivate the checks
    _worker2 = nil;
  }
  return self;
}

@end


@interface Child2 : Test {
  Worker *_worker1;
  Worker *_worker2;
}

@end

@implementation Child2

- (void)setDelegates
{
  _worker1.delegate = self;
  _worker2.delegate = self;
}

- (void)dealloc
{
  _worker1.delegate = nil;
}

@end
