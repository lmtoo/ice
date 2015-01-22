// **********************************************************************
//
// Copyright (c) 2003-2015 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#import <objc/Ice.h>
#import <facets/TestI.h>
#import <TestCommon.h>
#ifdef ICE_OBJC_GC
#   import <Foundation/NSGarbageCollector.h>
#endif

static int
run(id<ICECommunicator> communicator)
{
    [[communicator getProperties] setProperty:@"TestAdapter.Endpoints" value:@"default -p 12010"];
    id<ICEObjectAdapter> adapter = [communicator createObjectAdapter:@"TestAdapter"];
    ICEObject* d = ICE_AUTORELEASE([[TestFacetsDI alloc] init]);
    ICEObject* f = ICE_AUTORELEASE([[TestFacetsFI alloc] init]);
    ICEObject* h = ICE_AUTORELEASE([[TestFacetsHI alloc] init]);
    [adapter add:d identity:[communicator stringToIdentity:@"d"]];
    [adapter addFacet:d identity:[communicator stringToIdentity:@"d"] facet:@"facetABCD"];
    [adapter addFacet:f identity:[communicator stringToIdentity:@"d"] facet:@"facetEF"];
    [adapter addFacet:h identity:[communicator stringToIdentity:@"d"] facet:@"facetGH"];

    id<TestFacetsGPrx> facetsAllTests(id<ICECommunicator>);
    facetsAllTests(communicator);

    return EXIT_SUCCESS;
}

#if TARGET_OS_IPHONE
#  define main facetsServer
#endif

int
main(int argc, char* argv[])
{
    int status;
    @autoreleasepool
    {
        id<ICECommunicator> communicator = nil;
        @try
        {
            ICEInitializationData* initData = [ICEInitializationData initializationData];
            initData.properties = defaultServerProperties(&argc, argv);
#if TARGET_OS_IPHONE
            initData.prefixTable__ = [NSDictionary dictionaryWithObjectsAndKeys:
                                      @"TestFacets", @"::Test",
                                      nil];
#endif
            communicator = [ICEUtil createCommunicator:&argc argv:argv initData:initData];
            status = run(communicator);
        }
        @catch(ICEException* ex)
        {
            tprintf("%@\n", ex);
            status = EXIT_FAILURE;
        }

        if(communicator)
        {
            @try
            {
                [communicator destroy];
            }
            @catch(ICEException* ex)
            {
            tprintf("%@\n", ex);
                status = EXIT_FAILURE;
            }
        }
    }
#ifdef ICE_OBJC_GC
    [[NSGarbageCollector defaultCollector] collectExhaustively];
#endif
    return status;
}