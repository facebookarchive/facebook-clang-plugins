// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/foundation.h"

@interface TestPropertySynthesizeType: NSObject
@property (atomic, assign) NSString *name;
@end

@implementation TestPropertySynthesizeType {
  NSMutableString *_name;
}
@synthesize name = _name;
@end
