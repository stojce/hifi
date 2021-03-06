//
//  AssetClient.h
//  libraries/networking/src
//
//  Created by Ryan Huffman on 2015/07/21
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//


#ifndef hifi_AssetClient_h
#define hifi_AssetClient_h

#include <QString>

#include <DependencyManager.h>

#include "AssetUtils.h"
#include "LimitedNodeList.h"
#include "NLPacket.h"
#include "Node.h"

class AssetRequest;
class AssetUpload;

struct AssetInfo {
    QString hash;
    int64_t size;
};

using ReceivedAssetCallback = std::function<void(bool responseReceived, AssetServerError serverError, const QByteArray& data)>;
using GetInfoCallback = std::function<void(bool responseReceived, AssetServerError serverError, AssetInfo info)>;
using UploadResultCallback = std::function<void(bool responseReceived, AssetServerError serverError, const QString& hash)>;



class AssetClient : public QObject, public Dependency {
    Q_OBJECT
public:
    AssetClient();
    
    Q_INVOKABLE void init();

    Q_INVOKABLE AssetRequest* createRequest(const QString& hash, const QString& extension);
    Q_INVOKABLE AssetUpload* createUpload(const QString& filename);
    Q_INVOKABLE AssetUpload* createUpload(const QByteArray& data, const QString& extension);

private slots:
    void handleAssetGetInfoReply(QSharedPointer<NLPacket> packet, SharedNodePointer senderNode);
    void handleAssetGetReply(QSharedPointer<NLPacketList> packetList, SharedNodePointer senderNode);
    void handleAssetUploadReply(QSharedPointer<NLPacket> packet, SharedNodePointer senderNode);

    void handleNodeKilled(SharedNodePointer node);

private:
    bool getAssetInfo(const QString& hash, const QString& extension, GetInfoCallback callback);
    bool getAsset(const QString& hash, const QString& extension, DataOffset start, DataOffset end, ReceivedAssetCallback callback);
    bool uploadAsset(const QByteArray& data, const QString& extension, UploadResultCallback callback);

    static MessageID _currentID;
    std::unordered_map<SharedNodePointer, std::unordered_map<MessageID, ReceivedAssetCallback>> _pendingRequests;
    std::unordered_map<SharedNodePointer, std::unordered_map<MessageID, GetInfoCallback>> _pendingInfoRequests;
    std::unordered_map<SharedNodePointer, std::unordered_map<MessageID, UploadResultCallback>> _pendingUploads;
    
    friend class AssetRequest;
    friend class AssetUpload;
};

#endif
