// Copyright 2004-present Facebook. All Rights Reserved.

#import <Foundation/Foundation.h>

@class Test;

@interface Worker : NSObject

@property (atomic, assign) Test *delegate;

@end

@interface Test : NSObject
{
  Worker *_worker1;
}

@property (atomic, retain) Worker *worker2;

@property (atomic, retain) Worker *worker3;

@end

@implementation Test

- (void)setDelegates
{
  // This marks _worker1.delegate _worker2.delegate _worker3.delegate as "dangerous"
  _worker1.delegate = self;
  [self.worker2 setDelegate:self];
  [_worker3 setDelegate:self];
}

- (void)dealloc
{
  _worker1.delegate = nil;
  [_worker1 release];
  // In non-ARC mode, deallocs do not implicitly release, so we won't complain about the other unsafe references.
  // (This code will get a memory leak warning by another checker.)
}

- (void)ok1 {
  _worker1.delegate = nil;
  // was correctly cleared
  [_worker1 release];
}

- (void)ok2 {
  // this is considered legit too
  if (_worker2.delegate == self) {
    [self.worker2 setDelegate:nil];
  }
  [_worker2 release];
}

- (void)ok2bis {
  // same with only an assert
  assert(_worker2.delegate != self);
  [_worker2 release];
}


- (void)ok3 {
  [self.worker2 setDelegate:nil];
  // here property getters are considered equivalent to the ivar
  [_worker2 release];
}

- (void)ok4 {
  [_worker2 setDelegate:nil];
  [_worker2 setDelegate:self];
  [_worker2 setDelegate:nil];
  // the last assignement of the delegate wins
  [_worker2 release];
  _worker2 = nil;
}

- (void)ok5
{
  // setting a provably-null ivar is ok
  if (!_worker2) {
    _worker2 = [[Worker alloc] init];
  }
}

// These two setters should be correctly handled (LIMITATION:) provided that there are inlined by the analyzer.
- (void)setWorker2:(Worker *)value
{
  if (value == _worker2) {
    return;
  }
  if (self == _worker2.delegate) {
    [_worker2 setDelegate:nil];
  }
  [_worker2 release];
  _worker2 = [value retain];
}

- (void)ok6 {
  // the "safe" setter should be inlined (at least in simple cases)
  self.worker2 = nil;
}

- (void)setWorker3:(Worker *)value
{
  if (value == _worker3) {
    return;
  }
  [_worker3 setDelegate:nil];
  [_worker3 release];
  _worker3 = [value retain];
}

- (void)ok7 {
  [self setWorker3:nil];
  // next we know that either the delegate was cleared by the previous line or worker3 was already nil
  [_worker3 release];
  _worker3 = nil;
}

// Pseudo-init methods are considered innocent unless proven guilty
- (id)init
{
  // ok because we assume everything is cleared at the beginning of init methods
  if (self = [super init]) {
    [_worker1 release];
    _worker1 = [[Worker alloc] init];
  }
  return self;
}

- (void)setup
{
  // idem
  _worker2 = nil;
}

- (void)loadView
{
  // idem
  _worker2 = nil;
}


- (void)ok8 {
  if (_worker2.delegate == nil) {
    [_worker2 release];
    _worker2 = nil;
  }
}

- (void)ok9 {
  if (_worker2 == nil) {
    // In theory self.worker2 could do all sorts of side effects and populate _worker2. However our rule chooses to ignore this.
    NSLog(@"%@", self.worker2);
    [_worker2 release];
    _worker2 = nil;
  }
}

- (void)ok10 {
  if ([self.worker2 delegate] == nil) {
    _worker2 = nil;
  }
}

- (void)ok11 {
  if (self.worker2 == nil) {
    [_worker2 release];
    _worker2 = nil;
  }
}

- (void)ok8bis {
  _worker2.delegate = self;
  if (!_worker2.delegate) {
    [_worker2 release];
    _worker2 = nil;
  }
}

- (void)ok9bis {
  // this line forces the creation of a 'symbol' for _worker2
  _worker2.delegate = self;
  if (!_worker2) {
    // In theory self.worker2 could do all sorts of side effects and populate _worker2. However our rule chooses to ignore this.
    NSLog(@"%@", self.worker2);
    [_worker2 release];
    _worker2 = nil;
  }
}

- (void)ok10bis {
  _worker2.delegate = self;
  if (![self.worker2 delegate]) {
    _worker2 = nil;
  }
}

- (void)ok11bis {
  _worker2.delegate = self;
  if (!self.worker2) {
    [_worker2 release];
    _worker2 = nil;
  }
}
- (void)ok12 {
  // In non-ARC mode, pure assignments of ivars are ignored. We assume that assignments are preceded by calls to release.
  // E.g. the next line should trigger a leak warning but a "dangling delegate" warning.
  _worker1 = nil;
}

@end
