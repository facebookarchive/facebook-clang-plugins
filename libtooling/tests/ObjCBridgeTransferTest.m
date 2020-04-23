#include "FoundationStub.h"

typedef struct ABFDataRef {
} ABFDataRef;

@interface ABFData
@end

ABFDataRef* ABFDataCreate();

@interface A : NSObject
@end

@implementation A

- (void)bridge_transfer_example {
  ABFData* someData = (__bridge_transfer ABFData*)ABFDataCreate();
}

@end
