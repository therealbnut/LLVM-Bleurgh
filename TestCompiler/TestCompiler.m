//
//  TestCompiler.m
//  TestCompiler
//
//  Created by Andrew Bennett on 5/08/13.
//  Copyright (c) 2013 TeamBnut. All rights reserved.
//

#import "TestCompiler.h"

double bleurgh_main(void);

@implementation TestCompiler

- (void)setUp
{
    [super setUp];
    
    // Set-up code here.
}

- (void)tearDown
{
    // Tear-down code here.
    [super tearDown];
}

- (void) testThings
{
    printf("Output: %lf\n", bleurgh_main());
}

@end
