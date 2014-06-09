// Copyright 2004-present Facebook. All Rights Reserved.

#import <Foundation/foundation.h>

@interface Class1 : NSObject

@property (readonly, nonatomic) int value;

-(void)print;

@end

@interface Class2 : Class1

@property (nonatomic, assign, readwrite) int value;

@end

@interface Class3 : Class1

@property (nonatomic, assign, readwrite) int value;

@end
