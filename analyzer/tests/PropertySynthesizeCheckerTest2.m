// Copyright 2004-present Facebook. All Rights Reserved.

#import "Foundation/Foundation.h"

@protocol TestProtocol <NSObject>

@property (nonatomic, assign) NSString* test;

@end

@interface AbstractParent: NSObject<TestProtocol> {
  @protected int _duplicateWithoutSynthReadonlyProtectedWithExplicitSetter;
}

@property (nonatomic, assign) NSString* duplicateWithoutSynth;

@property (nonatomic, assign, readonly) NSString* duplicateWithoutSynthReadonly;

@property (nonatomic, readonly) int duplicateWithoutSynthReadonlyProtectedWithExplicitSetter;

@end

@interface TestInterface : AbstractParent

@property (nonatomic, assign) NSString* var;

@property (nonatomic, assign) NSString* duplicateWithoutSynth;

@property (nonatomic, assign, readonly) NSString* duplicateWithoutSynthReadonly;

@property (nonatomic, assign, readwrite) int duplicateWithoutSynthReadonlyProtectedWithExplicitSetter;

@end

@implementation TestInterface

@synthesize test;

@synthesize var = _myVar;

- (void)setDuplicateWithoutSynthReadonlyProtectedWithExplicitSetter:(int)value {
  _duplicateWithoutSynthReadonlyProtectedWithExplicitSetter = value;
}

@end

__attribute__((objc_requires_property_definitions))
@interface WithAttribute : NSObject
@property (nonatomic, assign) NSString* var;
@end

@implementation WithAttribute
@synthesize var = _var;
@end
