//
//  URLUtils.h
//  libraries/networking/src/networking
//
//  Created by Sabrina Shanman on 2018-12-14.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_URLUtils_h
#define hifi_URLUtils_h

#include <string>

#include "HifiTypes.h"

// Gets the data stored in a data URI (see RFC 2397), or no data if the URI is invalid
// TODO: Extend this function to provide the media type and encoding information if more complete data URI support is needed
hifi::ByteArray getDataFromURI(const std::string& uri);

#endif // hifi_URLUtils_h
