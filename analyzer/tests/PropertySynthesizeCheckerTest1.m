// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/Foundation.h"

@interface TestProtocoPropertySynthesize: NSObject
@property (atomic, assign) NSString *firstName;
@end

@implementation TestProtocoPropertySynthesize
@synthesize firstName = _firstName;
@end


