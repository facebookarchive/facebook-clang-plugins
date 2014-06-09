// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/Foundation.h"

@interface SomeInterface: NSObject
- (void)fun:(int)x value:(int)val;
@end

@implementation SomeInterface
- (void)testDirectCall1
{
  int x = 0;
  
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    x++;
    
    [NSObject randomMsgToClass:45];

    // OK same selector as in the condition
    [receiver someSel:3 withSomeParam:4];    
  }
  
  // OK it's not inside the if
  [receiver someSel:3 withSomeParamX:4];
}

- (void)testDirectCall2
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // OK instance method
    [receiver fun:3 value:4];
  }  
}

- (void)testDirectCall3
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'withSomeParamX' misspelled
    [receiver someSel:3 withSomeParamX:4];
  }  
}

- (void)testDirectCall4
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'valueX misspelled'
    [receiver fun:3 valueX:4];
  }  
}

- (void)testDirectCall5
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {  
    [receiver someSel:3 withSomeParam:4];    

    // OK shielded by previous call
    [receiver someSel:3 withSomeParamX:4];    
  }  
}

- (void)testPerformSelector1
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // OK same selector as in the condition
    [receiver performSelector:@selector(someSel:withSomeParam:) withObject:3 withObject:4];
  }
}

- (void)testPerformSelector2
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'withSomeParamX' misspelled
    [receiver performSelector:@selector(someSel:withSomeParamX:) withObject:3 withObject:4];
  }
}

- (bool)testContext1
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    bool b;
    
    // BAD 'withSomeParamX' misspelled
    b = ![receiver someSel:1 withSomeParamX:1];
    
    return b;
  }
  return true;
}
- (bool)testContext2
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    bool b = false;
    
    // BAD 'withSomeParamX' misspelled
    b = b || [receiver someSel:2 withSomeParamX:2];
    
    return b;
  }
  return true;
}

- (bool)testContext3
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'withSomeParamX' misspelled
    bool x = [receiver someSel:3 withSomeParamX:3];

    return x;
  }
  return true;
}

- (bool)testContext4
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'withSomeParamX' misspelled
    return [receiver someSel:4 withSomeParamX:4];
  }
  return true;
}

- (bool)testContext5
{
  SomeInterface *receiver = [[SomeInterface alloc] init];
  if ([receiver respondsToSelector:@selector(someSel:withSomeParam:)])
  {
    // BAD 'withSomeParamX' misspelled
    foo([receiver someSel:4 withSomeParamX:4]);
  }
  return true;
}


@end
