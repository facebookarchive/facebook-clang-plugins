// Copyright 2004-present Facebook. All Rights Reserved.


#import "Foundation/Foundation.h"

@interface myRuleTest : NSObject
@end

@implementation myRuleTest


-(BOOL)logicStuffOk {
    BOOL x = TRUE;
    BOOL y = TRUE;
    BOOL z = x && y;
    return z;
}

-(BOOL)logicStuffBad {
    BOOL x = TRUE;
    BOOL y = TRUE;
    BOOL w = x || x;
    return w;
}

-(int)bitStuffOk {
    int a = 1;
    int b = 2;
    int c = a | b;
    return c;
}

-(int)bitStuffBad {
    int a = 1;
    int b = 2;
    int d = b & b;
    return d;
}

@end
