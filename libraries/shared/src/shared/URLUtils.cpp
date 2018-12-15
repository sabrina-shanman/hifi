//
//  URLUtils.cpp
//  libraries/networking/src/networking
//
//  Created by Sabrina Shanman on 2018-12-14.
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "URLUtils.h"

#include <QVarLengthArray>

hifi::ByteArray getDataFromURI(const std::string& uri) {
    // First, check that this is a valid data uri
    const static std::string prefix = "data:";
    if (uri.find(prefix) != 0) {
        return hifi::ByteArray();
    }
    // Check where the data starts
    int dataDelimiterPos = uri.find(",", prefix.size());
    if (dataDelimiterPos == std::string::npos || dataDelimiterPos == uri.size() - 1) {
        // No idea where the data begins, or data is empty
        return hifi::ByteArray();
    }
    // Get the data, decoding as needed
    QByteArray encodedData = QByteArray(uri.substr(dataDelimiterPos+1).c_str());
    if (uri.rfind(";base64", dataDelimiterPos)) {
        // Decode from url-friendly base64
        return QByteArray::fromBase64(encodedData, QByteArray::Base64UrlEncoding);
    } else {
        // Un-escape characters normally escaped in a url/uri
        return QByteArray::fromPercentEncoding(encodedData);
    }
}
